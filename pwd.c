#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int pwd(void);
static int dirwalk(ino_t ino, int (*fnc)(void));

int main(int ac, char* av[]) {
    if (pwd() == -1) {
        exit(EXIT_FAILURE);
    }

    printf("\n");
    exit(EXIT_SUCCESS);
}

static int pwd(void) {
    struct stat chld_sb, par_sb;
    if (stat(".", &chld_sb) == -1) {
        perror("stat");
        return -1;
    }

    if (stat("..", &par_sb) == -1) {
        perror("stat");
        return -1;
    }

    if (chld_sb.st_ino == par_sb.st_ino) /* reach the root */
        return 0;

    if (chdir("..") == -1) {
        perror("chdir");
        return -1;
    }

    return dirwalk(chld_sb.st_ino, pwd);
}

static int dirwalk(ino_t ino, int (*fnc)(void)) {
    DIR* dir = NULL;
    struct dirent* dp = NULL;

    if ((dir = opendir(".")) == NULL) {
        perror("opendir");
        goto error;
    }

    for (;;) {
        errno = 0;
        dp = readdir(dir);
        if (dp == NULL) {
            if (!errno) {
                goto success;
            }

            perror("readdir");
            goto error;
        }

        if (dp->d_ino == ino) {
            if (fnc != NULL) {
                if ((*fnc)() == -1) {
                    goto error;
                }
            }

            printf("/%s", dp->d_name);
            break;
        }
    }
    goto success;

success:
    if (closedir(dir) == -1) {
        perror("closedir");
        return -1;
    }

    return 0;

error:
    if (dir != NULL) {
        if (closedir(dir) == -1) {
            perror("closedir");
        }
    }
    return -1;
}
