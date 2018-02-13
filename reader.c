#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <unistd.h>

#include "reader.h"

int read_files(struct read_config *conf)
{
	int i = 0;
	FILE *f = NULL;
	int is_additional_space = 0;

	for (i = optind; i < conf->ac; i++) {
		if (conf->argv[i] == NULL)
			continue;

		if ((f = fopen(conf->argv[i], "r")) == NULL) {
			fprintf(stderr, "fopen(%s): %s\n", conf->argv[i], strerror(errno));
			return -1;
		}

		if (conf->is_print) {
			if (is_additional_space)
				printf("\n");

			if (i != (conf->ac - 1))
				is_additional_space = 1;
			else
				is_additional_space = 0;

			printf("==> %s <==\n", conf->argv[i]);
		}

		if (conf->read_file(f) == -1) {
			if (fclose(f) == -1) {
				fprintf(stderr,
						"fclose(%s): %s\n", conf->argv[i], strerror(errno));
				return -1;
			}
		}

		if (fclose(f) == -1) {
			fprintf(stderr, "fclose(%s): %s\n", conf->argv[i], strerror(errno));
			return -1;
		}
	}

	return 0;
}

int parse_num(char *val, int *num)
{
	if (val == NULL)
		return 0;

	errno = 0;
	int n = atoi(val);
	if (errno != 0) {
		perror("atoi");
		return -1;
	}

	*num = n;
	return 0;
}

int read_and_print_bytes(FILE * f, size_t nmemb)
{
	char buf[nmemb];
	ssize_t n = 0;
	const int len = sizeof(buf) / sizeof(buf[0]);

	n = fread(buf, 1, len, f);
	if (ferror(f) != 0) {
		perror("fread");
		return -1;
	}

	if (fwrite(buf, 1, n, stdout) != n) {
		perror("fwrite");
		return -1;
	}

	return 0;
}

int file_len(FILE * f)
{
	if (fseek(f, 0, SEEK_END) == -1) {
		perror("fseek");
		return -1;
	}

	const int offset = ftell(f);
	if (offset == -1) {
		perror("ftell");
		return -1;
	}

	if (fseek(f, 0, SEEK_SET) == -1) {
		perror("fseek");
		return -1;
	}

	return offset;
}

ssize_t write_from_to(FILE * src, FILE * dst)
{
	char buf[BUFSIZ * 4];
	const int buf_len = BUFSIZ * 4;
	ssize_t n = 0;
	ssize_t w_len = 0;

	while ((n = fread(buf, 1, buf_len, src)) > 0) {
		if (ferror(src) != 0) {
			perror("fread");
			return -1;
		}

		if (fwrite(buf, 1, n, dst) != n) {
			perror("fwrite");
			return -1;
		}
		w_len += n;
	}

	return w_len;
}
