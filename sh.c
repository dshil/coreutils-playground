#include <stdio.h>
#include <errno.h>
#include <utmpx.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <ttyent.h>
#include <sys/wait.h>
#include <sys/types.h>

/* It's a basic and naive implementation of the UNIX bash.
 * It supports the following:
 *  - Run processes
 *  - If/Then/Else blocks
 *  - Pipes
 *  - Handle Ctrl-C
 */

static int execute(char **av, int *status);
static char **init_list(int sz);
static char** list_from_line(char *line, const char *delim, int *len);

static void sig_handler(int signum);
static void set_signals(void);

static char *get_host(void);
static char *get_curr_dir(void);
static void prompt(void);
static void trim_suffix(char *s);

static char *tty_name = NULL;
static char *cur_dir = NULL;
static char *host = NULL;

struct context {
	char **if_block;
	char **then_block;
	char **else_block;
	int state;
};

static int def_state = 0;
static int if_state = 1;
static int then_state = 2;
static int else_state = 3;
static int fi_state = 4;

static int add_cmd(struct context *ctx, char *cmd, int *sz, int *idx);

static void empty_context(struct context *ctx);
static void set_context_state(const char *cmd, struct context *ctx);
static int exec_context(struct context *ctx);
static void free_context(struct context *ctx);

static void free_block(char **blk);
static int exec_block(char **blk, int *status);
static char **block_by_state(struct context *ctx);
static void set_block_by_state(struct context *ctx, char **blk);
static int is_eob(int idx, int sz); // check the end of the commands block.

// Returns -1 in case of the error, corresponding command block will be freed.
static int add_eob(struct context *ctx, int idx, int sz);

static int is_control_cmd(char *cmd);
static char *cmd_by_state(int state);
static int is_valid_cmd(struct context *ctx, char *cmd);

struct command {
	char **args;
	int fd_in;
	int fd_out;
};

static int is_pipeline(char *line);
static int process_pipeline(char *line);
static int free_commands(struct command *cmds, int len, int started_num);
static int free_command(struct command *cmd);
static int run_pipeline(struct command *cmds, int len);
static int redirect_cmd_in_out(struct command *cmd);

int main(int ac, char *av[])
{
	set_signals();

	if ((tty_name = getlogin()) == NULL) {
		perror("getlogin");
		exit(EXIT_FAILURE);
	}

	if ((host = get_host()) == NULL)
		exit(EXIT_FAILURE);

	if ((cur_dir = get_curr_dir()) == NULL)
		exit(EXIT_FAILURE);

	char **arglist = NULL;
	char **cmdline = NULL;
	char **cp = NULL;
	char *cmd = NULL;
	char *line = NULL;
	size_t cap = 0;

	struct context ctx;
	empty_context(&ctx);

	int idx = 0;
	int sz = 1;

	for (;;) {
		if (ctx.state == 0)
			prompt();
		else
			printf("> ");

		if (getline(&line, &cap, stdin) == -1) {
			if (feof(stdin)) {
				printf("exit\n");
				break;
			}
			perror("getline");
			goto error;
		}

		if (strcmp(line, "\n") == 0)
			continue;

		if (strncmp(line, "exit", 4) == 0) {
			printf("exit\n");
			break;
		}

		trim_suffix(line);

		if (is_pipeline(line)) {
			if (process_pipeline(line) == -1)
				goto error;
			continue;
		}

		if (is_control_cmd(line)) {
			if (!is_valid_cmd(&ctx, line)) {
				fprintf(stderr, "%s: invalid command `%s` after `%s`\n", av[0],
						line, cmd_by_state(ctx.state));
				goto error;
			}

			if (ctx.state == def_state) {
				set_context_state(line, &ctx);
				continue;
			}

			if (add_eob(&ctx, idx, sz) == -1)
				goto error;

			set_context_state(line, &ctx);
			if (ctx.state == fi_state) {
				if (exec_context(&ctx) == -1)
					goto error;
				free_context(&ctx);
			}
			idx = 0;
			sz = 1;

			continue;
		}

		if ((cmdline = list_from_line(line, ";", NULL)) == NULL)
				goto error;

		if (ctx.state == def_state) {
			if (exec_block(cmdline, NULL) == -1) {
				free(cmdline);
				goto error;
			}

			continue;
		}

		cp = cmdline;
		while (*cp != NULL) {
			if (add_cmd(&ctx, *cp, &sz, &idx) == -1)
				goto error;
			cp++;
		}
		free(cmdline);
	}

	free(line);
	exit(EXIT_SUCCESS);

error:
	free(line);
	free_context(&ctx);
	exit(EXIT_FAILURE);
}

static int is_pipeline(char *line)
{
	return strchr(line, '|') != NULL;
}

/**
 * process_pipeline parses the @line and creates a list of pipeline commands to
 * run.
 */
static int process_pipeline(char *line)
{
	int len = 0;
	char **list = list_from_line(line, "|", &len);
	if (list == NULL)
		return -1;

	struct command cmds[len];
	int idx = 0;

	char **cp = list;
	while (*cp != NULL) {
		struct command c;
		c.args = list_from_line(*cp, " ", NULL);
		c.fd_in = -1;
		c.fd_out = -1;

		cmds[idx++] = c;
		cp++;
	}

	int i = 0;
	for (; i < len - 1; i++) {
		int fds[2];
		if (pipe(fds) == -1) {
			perror("pipe");
			goto error;
		}
		cmds[i].fd_out = fds[1];
		cmds[i+1].fd_in = fds[0];
	}

	int proc_num = 0;
	if ((proc_num = run_pipeline(cmds, len)) == -1)
		goto error;

	while (proc_num-- != 0) {
		if (wait(NULL) == -1) {
			perror("wait");
			goto error;
		}
	}

	free(list);
	return 0;
error:
	free_commands(cmds, len, proc_num);
	free(list);
	return -1;
}

/**
 * run_pipeline runs the pipeline specified in the @cmds. @len stands for the
 * number of commands to run in the pipeline. The caller is responsible for
 * waiting until all commands will be done.
 */
static int run_pipeline(struct command *cmds, int len)
{
	int proc_num = 0;
	pid_t pid = 0;
	int i = 0;

	for (; i < len; i++) {
		pid = fork();
		if (pid == -1) {
			perror("fork");
			goto error;
		} else if (pid == 0) {
			if (redirect_cmd_in_out(&cmds[i]) == -1)
				exit(EXIT_FAILURE);

			if (execvp(cmds[i].args[0], cmds[i].args) == -1) {
				fprintf(stderr, "sh: %s: command not found...\n",
						cmds[i].args[0]);
				exit(EXIT_FAILURE);
			}
		} else {
			if (free_command(&cmds[i]) == -1)
				goto error;
			proc_num++;
		}
	}

	return proc_num;

error:
	while (proc_num-- != 0) {
		if (wait(NULL) == -1) {
			perror("wait");
		}
	}
	return -1;
}

/**
 * redirect_cmd_in_out changes the stdin and stdout of the child process with
 * provided in the @cmd.
 */
static int redirect_cmd_in_out(struct command *cmd)
{
	if (cmd->fd_in != -1) {
		if (dup2(cmd->fd_in, STDIN_FILENO) == -1) {
			perror("dup2");
			goto error;
		}

		if (close(cmd->fd_in) == -1) {
			perror("close");
			cmd->fd_in = -1;
			goto error;
		}
	}

	if (cmd->fd_out != -1) {
		if (dup2(cmd->fd_out, STDOUT_FILENO) == -1) {
			perror("dup2");
			goto error;
		}

		if (close(cmd->fd_out) == -1) {
			perror("close");
			cmd->fd_out = -1;
			goto error;
		}
	}

	return 0;

error:
	if (cmd->fd_in != -1)
		if (close(cmd->fd_in) == -1)
			perror("close");

	if (cmd->fd_out != -1)
		if (close(cmd->fd_out) == -1)
			perror("close");

	return -1;
}

/**
 * free_commands frees the argument list of the command and closes both end of
 * the pipe. @started_num stands for number of successfully running procs, no
 * need to free such commands.
 */
static int free_commands(struct command *cmds, int len, int started_num)
{
	int n = len - started_num - 1;
	int i = 0;
	for (; i < n; i++) {
		if (free_command(&cmds[i]) == -1)
			return -1;
	}
	return 0;
}

static int free_command(struct command *cmd)
{
	free(cmd->args);
	if (cmd->fd_in != -1) {
		if (close(cmd->fd_in) == -1) {
			perror("close");
			cmd->fd_in = -1;
			goto error;
		}
	}

	if (cmd->fd_out != -1) {
		if (close(cmd->fd_out) == -1) {
			perror("close");
			cmd->fd_out = -1;
			goto error;
		}
	}

	return 0;

error:
	if (cmd->fd_in != -1)
		if (close(cmd->fd_in) == -1)
			perror("close");

	if (cmd->fd_out != -1)
		if (close(cmd->fd_out) == -1)
			perror("close");

	return -1;
}

static int execute(char **av, int *status)
{
	pid_t pid = 0;

	pid = fork();
	if (pid == -1) {
		perror("fork");
		return -1;
	} else if (pid == 0) {
		if (execvp(av[0], av) == -1) {
			fprintf(stderr, "sh: %s: command not found...\n",
					av[0]);
			return -1;
		}
	} else {
		if (wait(status) == -1) {
			perror("wait");
			return -1;
		}
	}

	return 0;
}

static char **init_list(int sz)
{
	char **al = (char **) malloc(sz * sizeof(char *));
	if (al == NULL) {
		fprintf(stderr, "malloc, no memory\n");
		return NULL;
	}
	return al;
}

static char** list_from_line(char *line, const char *delim, int *count)
{
	int sz = 1;
	char **al = init_list(sz);
	if (al == NULL)
		goto error;

	char **cp = NULL;
	int idx = 0;
	int len = 0;

	char *p = NULL;
	char *q = NULL;
	for (q = line; (p = strtok(q, delim)) != NULL; q = NULL) {
		if (idx == sz) {
			sz *= 2;

			cp = realloc(al, sz * sizeof(char *));
			if (cp == NULL) {
				fprintf(stderr, "realloc, no memory\n");
				goto error;
			}
			al = cp;
		}

		al[idx++] = p;
	}

	if (idx == sz) {
		cp = realloc(al, (sz + 1) * sizeof(char *));
		if (cp == NULL) {
			fprintf(stderr, "realloc, no memory\n");
			goto error;
		}
		al = cp;
	}

	if (count != NULL)
		*count = idx;

	al[idx] = NULL;
	return al;

error:
	free(al);
	return NULL;
}

static void empty_context(struct context *ctx)
{
	ctx->if_block = NULL;
	ctx->then_block = NULL;
	ctx->else_block = NULL;
	ctx->state = def_state;
}

static void set_context_state(const char *cmd, struct context *ctx)
{
	if (strcmp(cmd, "if") == 0)
		ctx->state = if_state;
	else if (strcmp(cmd, "then") == 0)
		ctx->state = then_state;
	else if (strcmp(cmd, "else") == 0)
		ctx->state = else_state;
	else if (strcmp(cmd, "fi") == 0)
		ctx->state = fi_state;
	else
		return;
}

static int is_control_cmd(char *cmd)
{
	return (strcmp(cmd, "if") == 0 || strcmp(cmd, "then") == 0 ||
			strcmp(cmd, "else") == 0 || strcmp(cmd, "fi") == 0);
}

static char *cmd_by_state(int state)
{
	if (state == if_state)
		return "if";
	if (state == then_state)
		return "then";
	if (state == else_state)
		return "else";
	if (state == fi_state)
		return "fi";
	return NULL;
}

static int is_valid_cmd(struct context *ctx, char *cmd)
{
	if (strcmp(cmd, "if") == 0)
		return ctx->state == def_state;
	if (strcmp(cmd, "then") == 0)
		return ctx->state == if_state && ctx->if_block != NULL;
	if (strcmp(cmd, "else") == 0)
		return ctx->state == then_state && ctx->if_block != NULL;

	return -1;
}

static int add_cmd(struct context *ctx, char *cmd, int *sz, int *idx)
{
	char **cp = NULL;
	char **blk = block_by_state(ctx);

	if (blk == NULL) {
		if ((cp = init_list(*sz)) == NULL)
			return -1;
		blk = cp;
		set_block_by_state(ctx, blk);
	}

	if (*idx == *sz) {
		*sz *= 2;
		if ((cp = realloc(blk, *sz * sizeof(char *))) == NULL) {
			fprintf(stderr, "realloc, no memory\n");
			return -1;
		}
		blk = cp;
		set_block_by_state(ctx, blk);
	}

	char *cmd_cp = malloc(sizeof(cmd) / sizeof(char));
	strcpy(cmd_cp, cmd);

	blk[*idx] = cmd_cp;
	*idx += 1;

	return 0;
}

static int exec_context(struct context *ctx)
{
	int status = 0;
	if (exec_block(ctx->if_block, &status) == -1)
		return -1;

	if (WIFEXITED(status)) {
		if (exec_block(ctx->then_block, &status) == -1)
			return -1;
	} else {
		if (exec_block(ctx->else_block, &status) == -1)
			return -1;
	}

	return 0;
}

static void free_context(struct context *ctx)
{
	if (ctx->if_block != NULL) {
		free_block(ctx->if_block);
	}
	if (ctx->else_block != NULL) {
		free_block(ctx->else_block);
	}
	if (ctx->then_block != NULL) {
		free_block(ctx->then_block);
	}
	empty_context(ctx);
}

static char **block_by_state(struct context *ctx)
{
	if (ctx->state == if_state)
		return ctx->if_block;
	else if (ctx->state == else_state)
		return ctx->else_block;
	else if (ctx->state == then_state)
		return ctx->then_block;
	else
		return NULL;
}

static void set_block_by_state(struct context *ctx, char **blk)
{
	if (ctx->state == if_state)
		ctx->if_block = blk;
	else if (ctx->state == else_state)
		ctx->else_block = blk;
	else if (ctx->state == then_state)
		ctx->then_block = blk;
	else
		return;
}

static int add_eob(struct context *ctx, int idx, int sz)
{
	char **blk = block_by_state(ctx);
	if (idx == sz) {
		char **cp = realloc(blk, (sz + 1) * sizeof(char *));
		if (cp == NULL) {
			fprintf(stderr, "realloc, no memory\n");

			int i = 0;
			for (; i < idx; i++) {
				free(*blk++);
			}
			free(blk);
			set_block_by_state(ctx, NULL);
			return -1;
		}
		blk = cp;
		set_block_by_state(ctx, blk);
	}
	blk[idx] = NULL;
	return 0;
}

static int exec_block(char **blk, int *status)
{
	char **arglist = NULL;
	while (*blk != NULL) {
		if ((arglist = list_from_line(*blk, " ", NULL)) == NULL)
			return -1;

		if (execute(arglist, status) == -1) {
			free(arglist);
			return -1;
		}

		blk++;
		free(arglist);
	}

	return 0;
}

static void free_block(char **blk)
{
	char **cp = blk;
	while (*cp != NULL) {
		free(*cp++);
	}
	free(blk);
}

static void set_signals(void)
{
	int sigs[] = {SIGINT, SIGQUIT};
	const int sig_num = sizeof(sigs)/sizeof(int);
	int i = 0;
	for (; i < sig_num; i++)
		signal(sigs[i], sig_handler);
}

static void sig_handler(int signum)
{
	if (signum == SIGINT || signum == SIGQUIT) {
		printf("\n");
		prompt();
		fflush(stdout);
	}
}

static void prompt(void)
{
	printf("[%s@%s %s]$ ", tty_name, host, cur_dir);
}

static char *get_host(void)
{
	static char buf[255];
	if (gethostname(buf, 255) == -1) {
		perror("gethostname");
		return NULL;
	}
	char *q = buf;
	char *h = strtok(q, ".");
	if (h == NULL) {
		perror("strtok");
		return NULL;
	}
	return h;
}

static char *get_curr_dir(void)
{
	static char buf[BUFSIZ];
	if (getcwd(buf, BUFSIZ) == NULL) {
		perror("getcwd");
		return NULL;
	}
	char *dir = NULL;
	char *p = NULL;
	char *q = NULL;
	for (q = buf; (p = strtok(q, "/")) != NULL; q = NULL)
		dir = p;

	return dir;
}

static void trim_suffix(char *s)
{
	s[strlen(s)-1] = '\0';
}
