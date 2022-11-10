#include "kernel/param.h"

#include "user/user.h"

#define STDIN_LINE_MAX_LEN 256

char *str_mallocpy(int, char *);
int readline(char **);
int build_exec_argv(char **, char **, char **, int, int);
int arr_contains(char **, int, char *);

int
main(int argc, char *argv[])
{
	char *exec_argv[MAXARG] , *readline_argv[MAXARG], *cmd;
	int rl_size, argv_size, exec_argv_size;

	argv++;
	argv_size = argc - 1;

	/*
	 * When giving an argv for exec(2), the first string in the argv is
	 * ignored.
	 */
	exec_argv[0] = str_mallocpy(1, "");

	rl_size = readline((char **) readline_argv);

	exec_argv_size = build_exec_argv((char **) exec_argv + 1,
			(char **) readline_argv,
			(char **) argv, rl_size, argv_size);
	exec_argv_size++;

	exec_argv[exec_argv_size] = 0;

	cmd = exec_argv[1];

	exec(cmd, (char **) exec_argv + 1);

	exit(0);
}

int
readline(char **arr)
{
	int ret, i;
	char buf[STDIN_LINE_MAX_LEN], *tok;

	i = 0;

	ret = read(0, &buf, STDIN_LINE_MAX_LEN);
	if (ret < 0) {
		printf("ERROR: read(stdin)\n");
		exit(1);
	}

	tok = strtok(buf, "\n");
	while (tok) {
		arr[i++] = str_mallocpy(strlen(tok), tok);
		tok = strtok(0, "\n");
	}

	return i - 1;
}

char *
str_mallocpy(int size, char *tok)
{
	char *x;

	x = (char *) malloc(size + 1);
	if (!x) {
		printf("ERROR: malloc(3)\n");
		exit(1);
	}

	strcpy(x, tok);

	return x;
}

int
build_exec_argv(char **exec, char **rl, char **argv, int rl_size, int argv_size)
{
	int i, ret, count;
	char *str;

	count = 0;

	for (i = 0; i < argv_size; i++) {
		str = argv[i];
		ret = arr_contains(rl, rl_size, str);
		if (!ret)
			exec[count++] = str;
	}

	for (i = 0; i < rl_size; i++) {
		str = rl[i];
		ret = arr_contains(argv, argv_size, str);
		if (!ret)
			exec[count++] = str;
	}

	return count;
}

int
arr_contains(char **arr, int size, char *str)
{
	int i, ret;

	for (i = 0; i < size; i++) {
		ret = strcmp(arr[i], str);
		if (ret == 0)
			return 1;
	}

	return 0;
}
