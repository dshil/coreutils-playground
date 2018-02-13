#include <time.h>
#include <utmpx.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

int main(int ac, char *av[])
{
	setutxent();		/* reset the database */

	struct utmpx *rec = NULL;

	struct tm *tm_info = NULL;
	const int tm_len = 20;
	char time_str[tm_len];
	time_t tv_sec = 0;

	for (;;) {
		errno = 0;
		rec = getutxent();

		if (rec == NULL) {
			if (errno) {
				perror("getutxent");
				goto error;
			}
			break;
		}

		if (rec->ut_type != USER_PROCESS)
			continue;

		tv_sec = (time_t) rec->ut_tv.tv_sec;
		if ((tm_info = localtime(&tv_sec)) == NULL) {
			perror("localtime");
			goto error;
		}

		strftime(time_str, tm_len, "%b %e %H:%M", tm_info);
		printf("%s\t%s\t%s\t(%s)\n",
		       rec->ut_user, rec->ut_line, time_str, rec->ut_host);
	}

	endutxent();		/* close the database */
	exit(EXIT_SUCCESS);

 error:
	endutxent();
	exit(EXIT_FAILURE);
}
