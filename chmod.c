#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/*
        TODO:
                1. Support for numerical mode.
                2. -c option.
                3. -R option.
*/

static int parse_mode(char* rec, mode_t* mode, int* perm_mode);
static int setmode(char utype, char mode, mode_t* m);
static int is_delimiter(char c);

static int set_user_access(char utype, char mode, mode_t* m);
static int set_group_access(char utype, char mode, mode_t* m);
static int set_other_access(char utype, char mode, mode_t* m);

static int change_perm(char* filename, mode_t* mode, void (*)(mode_t*, mode_t));

static void add_perm(mode_t* dst, mode_t src);
static void rm_perm(mode_t* dst, mode_t src);

int main(int ac, char* av[]) {
    if (ac == optind) {
        fprintf(stderr, "%s: missing operand\n", av[0]);
        exit(EXIT_FAILURE);
    }

    char* rec = *++av;
    char *p, *q = NULL;
    int perm_mode = 0;
    mode_t mode = 0;

    for (q = rec; (p = strtok(q, ",")) != NULL; q = NULL) {
        if (parse_mode(p, &mode, &perm_mode) == -1) {
            exit(EXIT_FAILURE);
        }
    }

    char* filename = NULL;
    while (--ac != optind) {
        filename = *++av;
        if (perm_mode == 1) {
            if (change_perm(filename, &mode, add_perm) == -1)
                exit(EXIT_FAILURE);
        }

        if (perm_mode == 2) {
            if (change_perm(filename, &mode, rm_perm) == -1) {
                exit(EXIT_FAILURE);
            }
        }

        if (chmod(filename, mode) == -1) {
            fprintf(stderr, "chmod(%s): %s\n", filename, strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    exit(EXIT_SUCCESS);
}

static int parse_mode(char* rec, mode_t* mode, int* perm_mode) {
    char* s_mode = rec;
    char* s_utype = rec;
    int i = 0; /* position of the ``=,-,+`` */
    for (;;) {
        if (is_delimiter(*s_mode)) {
            break;
        }
        i++;
        s_mode++;
    }

    char delimiter = *s_mode++;
    if (delimiter == '=') {
        *perm_mode = 0;
    } else if (delimiter == '+') {
        *perm_mode = 1;
    } else if (delimiter == '-') {
        *perm_mode = 2;
    } else {
        fprintf(stderr, "%s, invalid delimiter: %c\n", "chmod", delimiter);
        return -1;
    }

    if (is_delimiter(*s_utype)) {
        while (*s_mode != '\0') {
            if (setmode('u', *s_mode, mode) == -1)
                return -1;
            s_mode++;
        }

        return 0;
    }

    while (*s_utype != delimiter) {
        while (*s_mode != '\0') {
            if (setmode(*s_utype, *s_mode, mode) == -1)
                return -1;
            s_mode++;
        }
        s_utype++;
        s_mode = rec + i + 1;
    }
    return 0;
}

static int is_delimiter(char c) {
    return c == '=' || c == '+' || c == '-';
}

static int setmode(char utype, char mode, mode_t* m) {
    if (utype == 'u') {
        if (set_user_access(utype, mode, m) == -1) {
            return -1;
        }
    } else if (utype == 'g') {
        if (set_group_access(utype, mode, m) == -1) {
            return -1;
        }
    } else if (utype == 'o') {
        if (set_other_access(utype, mode, m) == -1) {
            return -1;
        }
    } else if (utype == 'a') {
        if (set_user_access(utype, mode, m) == -1
            || set_group_access(utype, mode, m) == -1
            || set_other_access(utype, mode, m) == -1) {
            return -1;
        }
    } else {
        fprintf(stderr, "%s: invalid owner type: `%c=%c`\n", "chmod", utype, mode);
        return -1;
    }

    return 0;
}

static int set_user_access(char utype, char mode, mode_t* m) {
    if (mode == 'r')
        *m |= S_IRUSR;
    else if (mode == 'w')
        *m |= S_IWUSR;
    else if (mode == 'x')
        *m |= S_IXUSR;
    else if (mode == 's')
        *m |= S_ISUID;
    else if (mode == 't')
        *m |= S_ISVTX;
    else {
        fprintf(stderr, "%s: invalid mode: `%c=%c`\n", "chmod", utype, mode);
        return -1;
    }

    return 0;
}

static int set_group_access(char utype, char mode, mode_t* m) {
    if (mode == 'r')
        *m |= S_IRGRP;
    else if (mode == 'w')
        *m |= S_IWGRP;
    else if (mode == 'x')
        *m |= S_IXGRP;
    else if (mode == 's')
        *m |= S_ISUID;
    else if (mode == 't')
        *m |= S_ISVTX;
    else {
        fprintf(stderr, "%s: invalid mode: `%c=%c`\n", "chmod", utype, mode);
        return -1;
    }

    return 0;
}

static int set_other_access(char utype, char mode, mode_t* m) {
    if (mode == 'r')
        *m |= S_IROTH;
    else if (mode == 'w')
        *m |= S_IWOTH;
    else if (mode == 'x')
        *m |= S_IXOTH;
    else if (mode == 's')
        *m |= S_ISUID;
    else if (mode == 't')
        *m |= S_ISVTX;
    else {
        fprintf(stderr, "%s: invalid mode: `%c=%c`\n", "chmod", utype, mode);
        return -1;
    }

    return 0;
}

static int change_perm(char* filename, mode_t* mode, void (*fn)(mode_t*, mode_t)) {
    struct stat sb;
    if (stat(filename, &sb) == -1) {
        fprintf(stderr, "stat(%s): %s\n", filename, strerror(errno));
        return -1;
    }
    (*fn)(mode, sb.st_mode);

    return 0;
}

static void add_perm(mode_t* dst, mode_t src) {
    *dst |= src;
}

static void rm_perm(mode_t* dst, mode_t src) {
    *dst = src ^ *dst;
}
