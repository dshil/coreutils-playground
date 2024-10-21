#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/*
        TODO:
                1. Multi-colon output.
                2. Colorful output (to distinguish dir and file).
                3. Sort option.
                4. Author option.
                4. Directory option.
                6. Inode option.
                7. Dereference option.
*/

struct flags {
    int dot;    /* print or not the hidden files */
    char sort;  /* the sorting type */
    int format; /* true - use long format, else - short format */
    int inode;  /* print inode number or not */

    char* dir; /* parent dir */
    int deep;  /* list subdirs recursively */

    int eod;       /* the last entry in the directory - we can print */
    size_t nidx;   /* current number of file names */
    size_t nsize;  /* max number of file names */
    char** names;  /* files names */
    blkcnt_t bcnt; /* number of 512B blocks allocated */
};

static void dirwalk(struct flags* f, void (*fcn)(char*, struct flags*));
static void fstraverse(char* dir, struct flags* f);
static void init_flag(struct flags* f);
static void new_flag(struct flags* src, struct flags* dst);
static void finfo(int print_inode, char* buf, char* name, struct stat* sb);

static void mode_to_str(mode_t m, char* buf);
static char* gid_to_name(gid_t gid);
static char* uid_to_name(uid_t uid);

static int stringcmp(const void* p1, const void* p2);
static int rev_stringcmp(const void* p1, const void* p2);

int main(int ac, char* av[]) {
    struct flags f;
    init_flag(&f);

    int opt = 0;
    while ((opt = getopt(ac, av, "aliUrR")) != -1) {
        switch (opt) {
        case 'a':
            f.dot = 1;
            break;
        case 'l':
            f.format = 1;
            break;
        case 'U':
            f.sort = 'd';
            break;
        case 'r':
            f.sort = 'r';
            break;
        case 'R':
            f.deep = 1;
            break;
        case 'i':
            f.inode = 1;
            break;
        default:
            exit(EXIT_FAILURE);
        }
    }

    if (ac == optind) {
        fstraverse(".", &f);
    } else {
        char** files = av + optind;
        struct flags nf;
        while (--ac >= optind) {
            new_flag(&f, &nf);
            fstraverse(*files++, &nf);
        }
    }

    exit(EXIT_SUCCESS);
}

static void init_flag(struct flags* f) {
    f->names = NULL;
    f->nidx = 0;
    f->nsize = 0;
    f->sort = 'n';
    f->format = 0;
    f->dot = 0;
    f->eod = 0;
    f->dir = NULL;
    f->deep = 0;
    f->bcnt = 0;
    f->inode = 0;
}

static void new_flag(struct flags* src, struct flags* dst) {
    init_flag(dst);
    dst->sort = src->sort;
    dst->format = src->format;
    dst->dot = src->dot;
    dst->deep = src->deep;
    dst->inode = src->inode;
}

static void dirwalk(struct flags* f, void (*fcn)(char*, struct flags*)) {
    errno = 0;
    DIR* dir = opendir(f->dir);
    if (dir == NULL) {
        fprintf(stderr, "opendir(%s): %s\n", f->dir, strerror(errno));
        goto exit;
    }

    struct dirent* dp = NULL;
    for (;;) {
        errno = 0;
        dp = readdir(dir);
        if (dp == NULL) {
            if (errno == 0) {
                f->eod = 1;
                (*fcn)(NULL, f);
                break;
            }

            fprintf(stderr, "readdir(%s): %s\n", f->dir, strerror(errno));
            goto exit;
        }

        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) {
            continue;
        }

        (*fcn)(dp->d_name, f);
    }
    goto exit;

exit:
    if (dir != NULL) {
        if (closedir(dir) == -1) {
            fprintf(stderr, "closedir(%s): %s\n", f->dir, strerror(errno));
        }
    }
    return;
}

static void fstraverse(char* fname, struct flags* f) {
    if (f->eod) {
        if (f->sort == 'n')
            qsort(f->names, f->nidx, sizeof(char*), stringcmp);
        if (f->sort == 'r')
            qsort(f->names, f->nidx, sizeof(char*), rev_stringcmp);
        if (f->deep)
            printf("%s:\n", f->dir);
        if (f->format) {
            // f->bcnt - number of 512B blocks allocated. We're interested in
            // the number of 1024B blocks allocated.
            printf("total %lu\n", (unsigned long)f->bcnt / 2);
        }

        int i = 0;
        char* name = NULL;
        while (f->nidx-- > 0) {
            name = f->names[i++];
            printf("%s\n", name);
            free(name);
        }

        f->eod = 0;
        free(f->names);
        return;
    }

    struct stat sb;
    int bl = strlen(fname) + 1;
    if (f->dir != NULL) {
        bl += strlen(f->dir) + 1;
    }

    int cmp = strcmp(fname, ".");
    if (!cmp) {
        bl += 1;
    }

    char buf[bl];
    if (cmp && f->dir != NULL) {
        strcpy(buf, f->dir);
        strcat(buf, "/");
        strcat(buf, fname);
    } else {
        strcpy(buf, fname);
    }

    if (stat(buf, &sb) == -1) {
        fprintf(stderr, "stat(%s): %s\n", buf, strerror(errno));
        return;
    }

    if (S_ISDIR(sb.st_mode)) {
        if (f->deep || f->dir == NULL) {
            struct flags nf;
            new_flag(f, &nf);
            nf.dir = buf;
            dirwalk(&nf, fstraverse);
            return;
        }
    }

    if (fname[0] == '.')
        if (!f->dot)
            return;

    if (f->nsize == 0) {
        // it's a first time, need to alloc some memory
        char** names = (char**)malloc(sizeof(char*));
        if (names == NULL) { /* handle the malloc error */
        }
        f->names = names;
        f->nsize = 1;
    }

    if (f->nidx >= f->nsize) { /* grow the array of file names */
        size_t nsz = f->nsize * 2;
        char** names = (char**)realloc(f->names, nsz * sizeof(char*));
        if (names == NULL) { /* handle the realloc error */
        }
        f->names = names;
        f->nsize = nsz;
    }

    char* name = NULL;
    if (f->format) {
        name = (char*)malloc(BUFSIZ);
        if (name == NULL) { /* handle the malloc error */
        }
        finfo(f->inode, name, fname, &sb);
    } else {
        if (f->inode) {
            static const int max_inode_num_cnt = 20;
            int len = strlen(fname) + 1 + max_inode_num_cnt;
            name = (char*)malloc(len);
            if (name == NULL) { /* handle the malloc error */
            }
            sprintf(name, "%lu %s", (unsigned long)sb.st_ino, fname);
        } else {
            name = (char*)malloc(strlen(fname) + 1);
            if (name == NULL) { /* handle the malloc error */
            }
            sprintf(name, "%s", fname);
        }
    }
    f->names[f->nidx++] = name;
    f->bcnt += sb.st_blocks;

    // ls <filename>
    if (f->dir == NULL) {
        f->eod = 1;
        fstraverse(NULL, f);
    }
}

static void finfo(int print_inode, char* buf, char* name, struct stat* sb) {
    static char smode[11];
    mode_to_str(sb->st_mode, smode);

    if (print_inode) {
        sprintf(buf, "%lu %s %4lu %-8s %-8s %8ld %.12s %s", (unsigned long)sb->st_ino,
                smode, (unsigned long)sb->st_nlink, uid_to_name(sb->st_uid),
                gid_to_name(sb->st_gid), (unsigned long)sb->st_size,
                4 + ctime(&sb->st_mtime), name);
    } else {
        sprintf(buf, "%s %4lu %-8s %-8s %8ld %.12s %s", smode,
                (unsigned long)sb->st_nlink, uid_to_name(sb->st_uid),
                gid_to_name(sb->st_gid), (unsigned long)sb->st_size,
                4 + ctime(&sb->st_mtime), name);
    }
}

static void mode_to_str(mode_t m, char* buf) {
    strcpy(buf, "----------");

    if (S_ISDIR(m))
        buf[0] = 'd';
    if (S_ISCHR(m))
        buf[0] = 'c';
    if (S_ISBLK(m))
        buf[0] = 'b';

    if (m & S_IRUSR)
        buf[1] = 'r';
    if (m & S_IWUSR)
        buf[2] = 'w';
    if (m & S_IXUSR)
        buf[3] = 'x';
    if (m & S_ISUID)
        buf[3] = 's';

    if (m & S_IRGRP)
        buf[4] = 'r';
    if (m & S_IWGRP)
        buf[5] = 'w';
    if (m & S_IXGRP)
        buf[6] = 'x';
    if (m & S_ISGID)
        buf[6] = 's';

    if (m & S_IROTH)
        buf[7] = 'r';
    if (m & S_IWOTH)
        buf[8] = 'w';
    if (m & S_IXOTH)
        buf[9] = 'x';
    if (m & S_ISVTX)
        buf[9] = 't';
}

static char* uid_to_name(uid_t uid) {
    static char numstr[10];
    struct passwd* pp = NULL;

    if ((pp = getpwuid(uid)) == NULL) {
        sprintf(numstr, "%d", uid);
        return numstr;
    }

    return pp->pw_name;
}

static char* gid_to_name(gid_t gid) {
    static char numstr[10];
    struct group* gp = NULL;

    if ((gp = getgrgid(gid)) == NULL) {
        sprintf(numstr, "%d", gid);
        return numstr;
    }

    return gp->gr_name;
}

// p1, p2 in the realiaty are `char **`, strcasecmp expects to get const char *
static int stringcmp(const void* p1, const void* p2) {
    const char* s1 = *((char**)p1);
    const char* s2 = *((char**)p2);
    return strcasecmp(s1, s2);
}

static int rev_stringcmp(const void* p1, const void* p2) {
    char* s1 = *((char**)p1);
    char* s2 = *((char**)p2);
    return strcasecmp(s2, s1);
}
