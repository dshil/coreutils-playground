#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static int more(FILE* fin, FILE* tty);
static size_t linesnum(FILE* tty, int rows);
static int tty_mode(int mode);
static int set_signal_handler(void);
static int set_term_winsz(void);
static void handle_sig(int signum);
static int set_term_settings(void);
static void reset_term_settings(void);

static sig_atomic_t nrows = 24;
static sig_atomic_t ncols = 80;
static int tty_fileno = 0;

int main(int ac, char* av[]) {
    if (atexit(reset_term_settings) == -1) {
        perror("atexit");
        exit(EXIT_FAILURE);
    }

    FILE* tty = fopen("/dev/tty", "r");
    FILE* fin = NULL;

    if (tty == NULL) {
        perror("/dev/tty");
        goto error;
    }
    tty_fileno = fileno(tty);

    if (set_term_winsz() == -1) {
        exit(EXIT_FAILURE);
    }

    if (set_term_settings() == -1) {
        fprintf(stderr, "set_term_settings: %s\n", strerror(errno));
        goto error;
    }

    if (set_signal_handler() == -1)
        exit(EXIT_FAILURE);

    if (ac == 1) {
        if (more(stdin, tty) == -1) {
            perror("stdin");
            goto error;
        }
    } else {
        int i = 0;

        for (i = 1; i < ac; i++) {
            if (av[i] == NULL) {
                continue;
            }
            printf("==> %s <==\n", av[i]);

            fin = fopen(av[i], "r");
            if (fin == NULL) {
                fprintf(stderr, "fopen(%s): %s\n", av[i], strerror(errno));
                goto error;
            }

            if (more(fin, tty) == -1) {
                goto error;
            }

            if (fclose(fin) == EOF) {
                fprintf(stderr, "fclose(%s): %s\n", av[i], strerror(errno));
                goto error;
            }
        }
    }

    if (tty != NULL) {
        if (fclose(tty) == EOF) {
            fprintf(stderr, "fclose(%s): %s\n", "/dev/tty", strerror(errno));
        }
    }
    exit(EXIT_SUCCESS);

error:
    if (fin != NULL) {
        if (fclose(fin) == EOF) {
            perror("fclose");
        }
    }

    if (tty != NULL) {
        if (fclose(tty) == EOF) {
            fprintf(stderr, "fclose(%s): %s\n", "/dev/tty", strerror(errno));
        }
    }
    exit(EXIT_FAILURE);
}

static int more(FILE* fin, FILE* tty) {
    int nl = 0; /* lines number */
    int c = 0;  /* current character */
    int n = 0;  /* lines to display determined by the user */

    // Copy of actual rows number to prevent a race during the signal handling.
    int cp_rows = 0;

    for (;;) {
        c = getc(fin);
        if (ferror(fin) != 0) {
            perror("getc");
            return -1;
        }

        if (feof(fin)) {
            break;
        }

        if (fputc(c, stdout) == EOF) {
            perror("fputs");
            return -1;
        }

        if (c == '\n') {
            nl++;

            cp_rows = nrows;

            if (nl == cp_rows) {
                n = linesnum(tty, cp_rows);
                if (n == -1) {
                    return -1;
                } else if (n == 0) {
                    break;
                } else {
                    nl -= n;
                }
            }
        }
    }

    return 0;
}

static size_t linesnum(FILE* tty, int rows) {
    fprintf(stderr, "\033[7m more? \033[m\n");
    int c = 0;

    for (;;) {
        c = getc(tty);
        if ((ferror(tty)) != 0) {
            return -1;
        }

        if (c == 'q') {
            return 0;
        } else if (c == ' ') {
            return rows;
        } else if (c == '\n') {
            return 1;
        }
    }
    return 0;
}

static int set_term_settings() {
    if (tty_mode(0) == -1) {
        return -1;
    }

    struct termios attr;
    if (tcgetattr(tty_fileno, &attr) == -1) {
        return -1;
    }

    attr.c_lflag &= ~ECHO;
    attr.c_lflag &= ~ICANON;

    if (tcsetattr(tty_fileno, TCSANOW, &attr) == -1) {
        return -1;
    }

    return 0;
}

// tty_mode saves the original tty settings and restores them if needed.
static int tty_mode(int mode) {
    static struct termios orig_mode;
    static int orig_flags;
    static int is_reseted;

    if (mode == 0) {
        if (tcgetattr(tty_fileno, &orig_mode) == -1) {
            perror("tcgetattr");
            return -1;
        }

        if ((orig_flags = fcntl(tty_fileno, F_GETFL)) == -1) {
            perror("fcntl");
            return -1;
        }
    } else {
        if (is_reseted) {
            return 0;
        }

        if (tcsetattr(tty_fileno, TCSANOW, &orig_mode) == -1) {
            perror("tcsetattr");
            return -1;
        }

        if (fcntl(tty_fileno, F_SETFL, orig_flags) == -1) {
            perror("fcntl");
            return -1;
        }

        is_reseted = 1;
    }

    return 0;
}

static int set_signal_handler(void) {
    int sigs[] = { SIGINT, SIGWINCH };
    const int siglen = sizeof(sigs) / sizeof(int);

    struct sigaction act;
    act.sa_handler = handle_sig;

    for (int i = 0; i < siglen; i++) {
        if (sigaction(sigs[i], &act, NULL) == -1) {
            perror("sigaction");
            return -1;
        }
    }

    act.sa_flags = SA_RESTART;
    if (sigaction(SIGCONT, &act, NULL) == -1) {
        perror("sigaction");
        return -1;
    }

    act.sa_flags |= SA_RESETHAND;
    if (sigaction(SIGTSTP, &act, NULL) == -1) {
        perror("sigaction");
        return -1;
    }

    return 0;
}

// tcsh and bash will reset a terminal settings to default if a process will be
// interrupted by a signal, but let's not rely on this. Need to reset the
// default terminal settings when SIGINT and SIGTSTP are received.
static void handle_sig(int signum) {
    switch (signum) {
    case SIGINT:
        if (tty_mode(1) == -1) {
            exit(EXIT_FAILURE);
        }
        exit(EXIT_SUCCESS);
    case SIGWINCH:
        if (set_term_winsz() == -1) {
            exit(EXIT_FAILURE);
        }
        break;
    case SIGCONT:
        if (set_term_settings() == -1) {
            exit(EXIT_FAILURE);
        }
        break;
    case SIGTSTP:
        if (tty_mode(1) == -1) {
            exit(EXIT_FAILURE);
        }
        if (raise(signum) != 0) {
            exit(EXIT_FAILURE);
        }
    }
}

static int set_term_winsz(void) {
    struct winsize wbuf;
    if (ioctl(tty_fileno, TIOCGWINSZ, &wbuf) == -1) {
        perror("ioctl");
        return -1;
    }

    nrows = wbuf.ws_row;
    ncols = wbuf.ws_col;
    return 0;
}

static void reset_term_settings(void) {
    if (tty_mode(1) == -1) {
        fprintf(stderr, "failed to reset terminal settings\n");
    }
}
