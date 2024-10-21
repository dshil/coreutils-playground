#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <uv.h>

#include "reader.h"

/* TODO:
 *  - Handle signals gracefully
 */

static int read_tail_lines(FILE* f);
static int read_tail_bytes(FILE* f);
static int read_tail_blocks(FILE* f);

static int read_stdin_tail(struct read_config* conf);
static void usage(char* prog_name);

static int listen_file_changes(uv_loop_t*, uv_fs_event_t*, char*);
static void
handle_fs_event(uv_fs_event_t* handle, const char* filename, int events, int status);

static ssize_t write_from_path(const char* filepath, int offset);

static int nlines = 10;
static int nbytes = -1;
static int nblocks = -1;

static sig_atomic_t offset = 0;

int main(int ac, char* av[]) {
    char* nlineval = NULL;
    char* nbyteval = NULL;
    char* nblockval = NULL;
    int suppress_file_name = 0;

    int is_following_file = 0;
    char* following_file = NULL;

    int opt = 0;
    while ((opt = getopt(ac, av, "qf:b:c:n:")) != -1) {
        switch (opt) {
        case 'q':
            suppress_file_name = 1;
            break;
        case 'f':
            following_file = optarg;
            break;
        case 'b':
            nblockval = optarg;
            break;
        case 'n':
            nlineval = optarg;
            break;
        case 'c':
            nbyteval = optarg;
            break;
        default:
            usage(av[0]);
            exit(EXIT_FAILURE);
        }
    }

    const int line_byte = (nlineval != NULL) && (nbyteval != NULL);
    const int line_block = (nlineval != NULL) && (nblockval != NULL);
    const int byte_block = (nbyteval != NULL) && (nblockval != NULL);

    if (line_byte || line_block || byte_block) {
        usage(av[0]);
        exit(EXIT_FAILURE);
    }

    if (parse_num(nlineval, &nlines) == -1)
        exit(EXIT_FAILURE);

    if (parse_num(nbyteval, &nbytes) == -1)
        exit(EXIT_FAILURE);

    if (parse_num(nblockval, &nblocks) == -1)
        exit(EXIT_FAILURE);

    struct read_config config;

    if (nblocks != -1)
        config.read_file = read_tail_blocks;
    else if (nbytes != -1)
        config.read_file = read_tail_bytes;
    else
        config.read_file = read_tail_lines;

    if (ac == optind && following_file == NULL) {
        if (read_stdin_tail(&config) == -1)
            exit(EXIT_FAILURE);
    } else {
        config.is_print = ((ac - optind) > 1) && !suppress_file_name;
        config.argv = av;
        config.ac = ac;

        if (read_files(&config) == -1)
            exit(EXIT_FAILURE);
    }

    uv_loop_t* loop = NULL;
    uv_fs_event_t* fs_event_req = NULL;

    if (following_file != NULL) {
        const ssize_t file_len = write_from_path(following_file, 0);
        if (file_len == -1)
            exit(EXIT_FAILURE);

        offset = file_len;
        loop = uv_default_loop();
        fs_event_req = malloc(sizeof(uv_fs_event_t));

        if (listen_file_changes(loop, fs_event_req, following_file) == -1)
            goto error;
    }

    free(fs_event_req);
    free(loop);
    exit(EXIT_SUCCESS);

error:
    free(fs_event_req);
    free(loop);
    exit(EXIT_FAILURE);
}

static int read_stdin_tail(struct read_config* conf) {
    FILE* tmp = tmpfile();
    if (tmp == NULL) {
        perror("tmpfile");
        goto error;
    }

    if (write_from_to(stdin, tmp) == -1)
        goto error;

    if (conf->read_file(tmp) == -1)
        goto error;

    if (fclose(tmp) == -1) {
        perror("fclose");
        goto error;
    }

    return 0;

error:
    if (tmp != NULL) {
        if (fclose(tmp) == -1) {
            perror("fclose");
        }
    }
    return -1;
}

static int read_tail_lines(FILE* f) {
    int c = 0;
    int n = 0;
    int nc = 0; /* number of characters to print */

    if (fseek(f, 0, SEEK_END) == -1) {
        perror("fseek");
        return -1;
    }

    do {
        if (fseek(f, -1, SEEK_CUR) == -1) {
            // the beginning of the file was reached, it's time to break
            break;
        }

        c = getc(f);
        if (ferror(f) != 0) {
            perror("getc");
            return -1;
        }

        if (c == '\n' && (++n == (nlines + 1)))
            continue;

        nc++;
        if (ungetc(c, f) == EOF) {
            perror("ungetc");
            return -1;
        }

    } while (n != (nlines + 1));

    return read_and_print_bytes(f, nc);
}

static int read_tail_bytes(FILE* f) {
    int len = 0;
    if ((len = file_len(f)) == -1)
        return -1;

    if (len == 0)
        return 0;

    if (nbytes > len)
        return read_and_print_bytes(f, len);

    if (fseek(f, -nbytes, SEEK_END) == -1) {
        perror("fseek");
        return -1;
    }

    return read_and_print_bytes(f, nbytes);
}

static int read_tail_blocks(FILE* f) {
    if (fseek(f, 0, SEEK_END) == -1) {
        perror("fseek");
        return -1;
    }

    // 512, 256, 128, ..., 1.
    char block_sizes[9];
    const int blk_nums = sizeof(block_sizes) / sizeof(block_sizes[0]);

    int i = 0;
    int block_size = 512;
    for (;;) {
        // successfully read the required number of 512 bytes blocks.
        if (block_size == 512 && (block_sizes[i] == nblocks))
            break;

        if (fseek(f, -block_size, SEEK_CUR) == -1) {
            // Try a block with the less size. If the block size is equal to 1,
            // it's time to break.
            if (++i == blk_nums)
                break;

            block_size /= 2;
            continue;
        }
        block_sizes[i]++;
    }

    i = 0;
    block_size = 512;
    int size = 0;

    for (i = 0; i < blk_nums; i++) {
        size += block_size * block_sizes[i];
        block_size /= 2;
    }

    return read_and_print_bytes(f, size);
}

static void usage(char* prog_name) {
    fprintf(stderr,
            "Usage: %s [-q] [-f file] [-b blocks | -c bytes | -n lines]"
            " [file ...]\n",
            prog_name);
}

static int
listen_file_changes(uv_loop_t* loop, uv_fs_event_t* fs_event_req, char* filename) {
    int ret = uv_fs_event_init(loop, fs_event_req);
    if (ret != 0) {
        fprintf(stderr, "uv_fs_event_init: %s\n", uv_strerror(ret));
        return -1;
    }

    ret = uv_fs_event_start(fs_event_req, handle_fs_event, filename, 0);
    if (ret != 0) {
        fprintf(stderr, "uv_fs_event_start: %s\n", uv_strerror(ret));
        return -1;
    }

    ret = uv_run(loop, UV_RUN_DEFAULT);
    if (ret != 0) {
        fprintf(stderr, "uv_run: %s\n", uv_strerror(ret));
        return -1;
    }

    return 0;
}

static void
handle_fs_event(uv_fs_event_t* handle, const char* filename, int events, int status) {
    if (events & UV_CHANGE) {
        size_t off = offset;
        if ((off = write_from_path(filename, off)) != -1) {
            offset += off;
        }
    }
}

static ssize_t write_from_path(const char* filepath, int offset) {
    FILE* f = fopen(filepath, "r");
    if (f == NULL) {
        perror("fopen");
        goto error;
    }

    if (offset) {
        if (fseek(f, offset, SEEK_SET) == -1) {
            perror("fseek");
            goto error;
        }
    }

    ssize_t n = 0;

    if ((n = write_from_to(f, stdout)) == -1)
        goto error;

    if (fclose(f) == -1) {
        perror("fclose");
        return -1;
    }

    return n;

error:
    if (f != NULL) {
        if (fclose(f) == -1)
            perror("fclose");
    }
    return -1;
}
