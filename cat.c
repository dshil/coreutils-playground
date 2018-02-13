#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>

#include "reader.h"

static int read_cat(FILE * f);

static int is_print_num = 0;

int main(int ac, char *av[])
{
	int suppress_file_name = 0;

	int opt = 0;
	while ((opt = getopt(ac, av, "qn")) != -1) {
		switch (opt) {
		case 'q':
			suppress_file_name = 1;
			break;
		case 'n':
			is_print_num = 1;
			break;
		default:
			fprintf(stderr, "Usage %s [-q] [file ...]\n", av[0]);
			exit(EXIT_FAILURE);
		}
	}

	struct read_config config;
	config.read_file = read_cat;

	if (ac == optind) {
		if (config.read_file(stdin) == -1)
			exit(EXIT_FAILURE);
	} else {
		config.is_print = ((ac - optind) > 1) && !suppress_file_name;
		config.argv = av;
		config.ac = ac;

		if (read_files(&config) == -1)
			exit(EXIT_FAILURE);
	}

	exit(EXIT_SUCCESS);
}

static int read_cat(FILE * f)
{
	int ln = 1;
	int is_new_line = 1;

	int c = 0;
	for (;;) {
		c = getc(f);
		if (ferror(f) != 0) {
			perror("getc");
			return -1;
		}

		if (feof(f))
			break;

		if (is_new_line) {
			if (is_print_num) {
				printf("  %d  ", ln);
				ln++;
				is_new_line = 0;
			}
		}

		if (putchar(c) == EOF) {
			perror("putchar");
			return -1;
		}

		if (c == '\n')
			is_new_line = 1;

	}

	return 0;
}
