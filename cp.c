#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "reader.h"

/*
        TODO:
        - support for the following options:
                - [] rework error message reporting.
                        e.g. cp: cannot stat 'foo': No such file or directory.
                - [] f
                - [] R
                - [] v
                - [] X
        - handle C-Z correctly.
*/

static int cpdir(char* src_path, char* dst_path);
static int cpf(char* src_path, char* src_name, char* dst_path);

static int overwrite_file(char* filename);
static int gc(FILE* rf, char* r_file, FILE* wfd, char* w_file);
static void show_usage(void);

static const int MAXBUFSIZ = BUFSIZ * 4;

int main(int ac, char* av[]) {
    if (ac < 2) {
        show_usage();
        exit(EXIT_FAILURE);
    }

    if (av[ac - 2] == NULL || av[ac - 1] == NULL) {
        show_usage();
        exit(EXIT_FAILURE);
    }

    if (strcmp(av[ac - 2], av[ac - 1]) == 0) {
        fprintf(stderr, "cp: %s and %s are identical (not copied)\n", av[1], av[2]);
        exit(EXIT_FAILURE);
    }

    bool is_file_overwrite = false;
    int opt = 0;

    while ((opt = getopt(ac, av, "i")) != -1) {
        switch (opt) {
        case 'i':
            is_file_overwrite = true;
            break;
        default:
            fprintf(stderr, "Usage: %s [i] source_file target_file\n", av[0]);
            exit(EXIT_FAILURE);
        }
    }

    char* src_path = av[ac - 2];
    char* dst_path = av[ac - 1];

    if (is_file_overwrite) {
        int overwrite = overwrite_file(dst_path);

        if (overwrite == -1) {
            exit(EXIT_FAILURE);
        }

        if (overwrite == 0) {
            fprintf(stderr, "not overwritten\n");
            exit(EXIT_SUCCESS);
        }
    }

    if (cpdir(src_path, dst_path) == -1) {
        exit(EXIT_FAILURE);
    }

    exit(EXIT_SUCCESS);
}

static int cpdir(char* src_path, char* dst_path) {
    struct dirent* d_ptr = NULL;
    char* path = NULL;
    const int src_len = strlen(src_path);

    errno = 0;
    DIR* dir = opendir(src_path);
    if (dir == NULL) {
        if (errno == ENOTDIR) { /* suppose that it's a file. */
            if (cpf(src_path, NULL, dst_path) == -1) {
                goto error;
            }
            return 0;
        }
    }

    for (;;) {
        errno = 0;
        d_ptr = readdir(dir);
        if (d_ptr == NULL) {
            if (errno == 0) {
                break;
            }

            fprintf(stderr, "readdir(%s): %s\n", src_path, strerror(errno));
            goto error;
        }

        if (d_ptr->d_name[0] == '.')
            continue;

        path = (char*)malloc(src_len + strlen(d_ptr->d_name) + 3);
        strcpy(path, src_path);
        if (*(src_path + src_len - 1) != '/') {
            strcat(path, "/");
        }
        strcat(path, d_ptr->d_name);

        if (cpf(path, d_ptr->d_name, dst_path) == -1) {
            goto error;
        }

        free(path);
    }

    if (closedir(dir) == -1) {
        fprintf(stderr, "closedir(%s): %s\n", src_path, strerror(errno));
        return -1;
    }

    return 0;

error:
    free(path);
    if (dir != NULL) {
        if (closedir(dir) == -1) {
            fprintf(stderr, "closedir(%s): %s\n", src_path, strerror(errno));
        }
    }
    return -1;
}

static int cpf(char* src_path, char* src_name, char* dst_path) {
    const int dst_len = strlen(dst_path);
    char dst_buf[strlen(src_path) + dst_len + 3];

    strcpy(dst_buf, dst_path);
    if (*(dst_path + dst_len - 1) != '/') {
        strcat(dst_buf, "/");
    }
    if (src_name == NULL) {
        strcat(dst_buf, src_path);
    } else {
        strcat(dst_buf, src_name);
    }

    if (access(dst_path, F_OK) != -1) {
        struct stat dsb;
        if (stat(dst_path, &dsb) == -1) {
            fprintf(stderr, "stat(%s): %s\n", dst_path, strerror(errno));
            return -1;
        }

        if (S_ISDIR(dsb.st_mode))
            dst_path = dst_buf;
    }

    FILE* fin = fopen(src_path, "r");
    FILE* fout = NULL;
    if (fin == NULL) {
        fprintf(stderr, "fopen(%s): %s\n", src_path, strerror(errno));
        goto error;
    }

    fout = fopen(dst_path, "w");
    if (fout == NULL) {
        fprintf(stderr, "fopen(%s): %s\n", dst_path, strerror(errno));
        goto error;
    }

    if (write_from_to(fin, fout) == -1) {
        goto error;
    }

    return gc(fin, src_path, fout, dst_path);

error:
    if (fin != NULL) {
        if (fclose(fin) == -1) {
            fprintf(stderr, "fclose(%s): %s\n", src_path, strerror(errno));
        }
    }
    if (fout != NULL) {
        if (fclose(fout) == -1) {
            fprintf(stderr, "fclose(%s): %s\n", dst_path, strerror(errno));
        }
    }
    return -1;
}

static int overwrite_file(char* filename) {
    FILE* tty = NULL;

    if (access(filename, F_OK) != -1) {
        // ask user about overwrite the existing file or not.
        fprintf(stderr, "overwrite %s? (y/n [n]) ", filename);

        tty = fopen("/dev/tty", "r");
        if (tty == NULL) {
            fprintf(stderr, "fopen(/dev/tty): %s\n", strerror(errno));
            goto error;
        }

        int c = getc(tty);
        if (ferror(tty) != 0) {
            perror("getc");
            goto error;
        }

        if (tolower(c) != 'y') {
            // not overwritten
            if (fclose(tty) == EOF) {
                fprintf(stderr, "fclose(/dev/tty): %s\n", strerror(errno));
                return -1;
            }
            return 0;
        }
    }

    if (tty != NULL) {
        if (fclose(tty) == EOF) {
            fprintf(stderr, "fclose(/dev/tty): %s\n", strerror(errno));
            return -1;
        }
    }

    return 1;

error:
    if (tty != NULL) {
        if (fclose(tty) == EOF) {
            fprintf(stderr, "fclose(/dev/tty): %s\n", strerror(errno));
        }
    }

    return -1;
}

static int gc(FILE* rf, char* r_file, FILE* wf, char* w_file) {
    int is_err = 0;

    if (rf != NULL) {
        if ((fclose(rf)) == -1) {
            is_err = -1;
            fprintf(stderr, "fclose(%s): %s\n", r_file, strerror(errno));
        }
    }

    if (wf != NULL) {
        if ((fclose(wf)) == -1) {
            is_err = -1;
            fprintf(stderr, "fclose(%s): %s\n", w_file, strerror(errno));
        }
    }

    return is_err;
}

static void show_usage() {
    fprintf(stderr, "usage: %s\n%39s\n", "source_file target_file",
            "source_file ... target_directory");
}
