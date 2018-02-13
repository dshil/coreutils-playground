#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>

static int pipe_close(int ch[]);
static int dup2pipe(int ch[]);

int main(int ac, char *av[])
{
	int ch[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, ch) == -1) {
		perror("socketpair");
		exit(EXIT_FAILURE);
	}

	FILE *fin = NULL;
	FILE *fout = NULL;

	pid_t pid = 0;
	pid = fork();

	if (pid == -1) {
		perror("fork");
		pipe_close(ch);
		exit(EXIT_FAILURE);
	} else if (pid == 0) {
		if (dup2pipe(ch) == -1)
			exit(EXIT_FAILURE);
		if (execlp("dc", "dc", "-", NULL) == -1)
			exit(EXIT_FAILURE);
	} else {
		if (close(ch[0]) == -1) {
			perror("close");
			goto error;
		}

		char buf[BUFSIZ];
		char op = 0;
		int num1 = 0;
		int num2 = 0;

		fout = fdopen(ch[1], "w");
		fin = fdopen(ch[1], "r");
		if (fout == NULL || fin == NULL) {
			perror("fdopen");
			goto error;
		}

		for (;;) {
			fgets(buf, BUFSIZ, stdin);
			if (feof(stdin))
				break;
			if (ferror(stdin) != 0) {
				perror("fgets");
				goto error;
			}

			if (sscanf(buf, "%d%[-+*/^]%d", &num1, &op, &num2) != 3) {
				fprintf(stderr,
					"invalid format, use num1 op num2\n");
				continue;
			}

			if (fprintf(fout, "%d\n%d\n%c\np\n", num1, num2, op) == EOF) {
				perror("fprintf");
				goto error;
			}

			if (fflush(fout) == -1) {
				perror("fflush");
				goto error;
			}

			fgets(buf, BUFSIZ, fin);
			if (ferror(fin) != 0) {
				fprintf(stderr, "empty dc response\n");
			} else {
				printf("%d %c %d = %s\n", num1, op, num2, buf);
			}
		}
	}

	if (fclose(fout) == -1) {
		perror("fclose");
		fclose(fin);
		exit(EXIT_FAILURE);
	}

	if (fclose(fin) == -1) {
		perror("fclose");
		exit(EXIT_FAILURE);
	}

	if (wait(NULL) == -1) {
		perror("wait");
		exit(EXIT_FAILURE);
	}

	exit(EXIT_SUCCESS);
 error:
	if (fin != NULL)
		fclose(fin);
	if (fout != NULL)
		fclose(fout);
	if (ch[0] != -1)
		close(ch[0]);
	if (ch[1] != -1)
		close(ch[1]);

	wait(NULL);
	exit(EXIT_FAILURE);
}

static int pipe_close(int ch[])
{
	if (close(ch[0]) == -1) {
		perror("close");
		close(ch[1]);
		return -1;
	}

	if (close(ch[1]) == -1) {
		perror("close");
		return -1;
	}

	return 0;
}

static int dup2pipe(int ch[])
{
	if (dup2(ch[0], STDIN_FILENO) == -1) {
		perror("dup2");
		goto error;
	}

	if (dup2(ch[0], STDOUT_FILENO) == -1) {
		perror("dup2");
		goto error;
	}

	if (close(ch[0]) == -1) {
		perror("close");
		goto error;
	}

	if (close(ch[1]) == -1) {
		perror("close");
		goto error;
	}

	return 0;

 error:
	if (ch[0] != -1)
		close(ch[0]);
	if (ch[1] != -1)
		close(ch[1]);
	return -1;
}
