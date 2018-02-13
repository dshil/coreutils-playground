#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <inttypes.h>

static int chwn(const char *owner, const char *grp, const char *fname);
static int parse_name(const char *name, uid_t * uid, gid_t * gid);
static gid_t name_to_gid(const char *grp);
static gid_t get_file_group(const char *grp, const char *fname);

int main(int ac, char *av[])
{
	if (ac == 1) {
		fprintf(stderr, "%s: missing operand\n", av[0]);
		exit(EXIT_FAILURE);
	}

	char *p = NULL;
	char *q = NULL;
	char *owner = NULL;
	char *grp = NULL;
	int i = 0;

	for (q = *(av + 1); (p = strtok(q, ":")) != NULL; q = NULL) {
		if (i == 0) {
			owner = p;
		} else if (i == 1) {
			grp = p;
		} else {
			fprintf(stderr, "%s: invalid format, <owner>:<group>\n",
				av[0]);
			exit(EXIT_FAILURE);
		}
		i++;
	}

	if (ac == 2) {
		fprintf(stderr, "%s: missing operand after `%s\n", av[0],
			owner);
		exit(EXIT_FAILURE);
	}

	char **files = av + 2;
	while (--ac > optind) {
		if (chwn(owner, grp, *files++) == -1)
			exit(EXIT_FAILURE);
	}
	exit(EXIT_SUCCESS);
}

static int chwn(const char *owner, const char *grp, const char *fname)
{
	uid_t uid = 0;
	gid_t gid = 0;

	if (grp == NULL) {
		// Use existing file group.
		if ((gid = get_file_group(grp, fname)) == -1)
			return -1;
	} else {
		int ogrp = strtol(grp, NULL, 10);
		if (!ogrp) {
			// Group has a string representation. Need to have a numeric.
			if ((gid = name_to_gid(grp)) == -1)
				return -1;
		} else {
			gid = ogrp;
		}
	}

	int onum = strtol(owner, NULL, 10);
	if (!onum) {
		// Owner has a string representation. Need to have a numeric.
		if (parse_name(owner, &uid, &gid) == -1)
			return -1;
	} else {
		uid = onum;
	}

	if (chown(fname, uid, gid) == -1) {
		fprintf(stderr, "chown(%s): %s\n", fname, strerror(errno));
		return -1;
	}
	return 0;
}

static int parse_name(const char *name, uid_t * uid, gid_t * gid)
{
	errno = 0;
	struct passwd *pw = getpwnam(name);
	if (pw == NULL) {
		if (errno == 0) {
			fprintf(stderr, "invalid user: %s\n", name);
			return -1;
		}
		perror("getpwnam");
		return -1;
	}

	*uid = pw->pw_uid;
	if (*gid == 0)
		*gid = pw->pw_gid;

	return 0;
}

static gid_t get_file_group(const char *grp, const char *fname)
{
	gid_t gid = 0;
	struct stat sb;
	if (stat(fname, &sb) == -1) {
		fprintf(stderr, "stat(%s): %s\n", fname, strerror(errno));
		return -1;
	}

	gid = sb.st_gid;
	return gid;
}

static gid_t name_to_gid(const char *grp)
{
	errno = 0;
	struct group *g = getgrnam(grp);
	if (g == NULL) {
		if (errno == 0) {
			fprintf(stderr, "invalid group: %s\n", grp);
			return -1;
		}
		fprintf(stderr, "getgrnam(%s): %s\n", grp, strerror(errno));
		return -1;
	}

	return g->gr_gid;
}
