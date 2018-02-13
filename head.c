#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>

#include "reader.h"

static int read_head_lines(FILE * f);
static int read_head_bytes(FILE * f);

static int nlines = 10;
static int nbytes = -1;

int main(int ac, char *av[])
{
	char *nlineval = NULL;
	char *nbyteval = NULL;
	int suppress_file_name = 0;

	int opt = 0;
	while ((opt = getopt(ac, av, "qn:c:")) != -1) {
		switch (opt) {
		case 'n':
			nlineval = optarg;
			break;
		case 'c':
			nbyteval = optarg;
			break;
		case 'q':
			suppress_file_name = 1;
			break;
		default:
			fprintf(stderr,
				"Usage: %s [-q] [-n lines | -c bytes] [file ...]\n", av[0]);
			exit(EXIT_FAILURE);
		}
	}

	if (nlineval != NULL && nbyteval != NULL) {
		fprintf(stderr,
			"Usage: %s [-q] [-n lines | -c bytes] [file ...]\n", av[0]);
		exit(EXIT_FAILURE);
	}

	if (parse_num(nlineval, &nlines) == -1)
		exit(EXIT_FAILURE);

	if (parse_num(nbyteval, &nbytes) == -1)
		exit(EXIT_FAILURE);

	struct read_config config;
	if (nbytes != -1)
		config.read_file = read_head_bytes;
	else
		config.read_file = read_head_lines;

	if (ac == optind) {
		if (config.read_file(stdin) == -1) {
			exit(EXIT_FAILURE);
		}
	} else {
		config.is_print = ((ac - optind) > 1) && !suppress_file_name;
		config.argv = av;
		config.ac = ac;

		if (read_files(&config) == -1) {
			exit(EXIT_FAILURE);
		}
	}

	exit(EXIT_SUCCESS);
}

static int read_head_lines(FILE * f)
{
	int c = 0;
	int n = 0; // number of lines that were already read

	while ((c = getc(f)) != EOF) {
		if (ferror(f) != 0) {
			perror("ferror");
			return -1;
		}

		if (c == '\n') {
			if (++n == nlines) {
				if (putchar(c) == EOF) {
					perror("putchar");
					return -1;
				}

				break;
			}

		}

		if (putchar(c) == EOF) {
			perror("putchar");
			return -1;
		}
	}

	return 0;
}

static int read_head_bytes(FILE * f)
{

	int len = 0;
	if ((len = file_len(f)) == -1)
		return -1;

	if (len == 0)
		return 0;

	return (nbytes > len) ? read_and_print_bytes(f, len) :
	    read_and_print_bytes(f, nbytes);
}
