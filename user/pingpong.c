#include "user/user.h"

void parent(int, int);
void child(int, int);

#define PIPE_READ_FD_IDX 0
#define PIPE_WRITE_FD_IDX 1

int
main(int argc, char *argv[])
{
	int ret, pid, ping_fd[2], pong_fd[2];

	if (argc != 1) {
		printf("Error, Usage: %s\n", argv[0]);
		exit(1);
	}

	ret = pipe(ping_fd);
	if (ret < 0) {
		printf("Error: pipe(2)\n");
		exit(1);
	}

	ret = pipe(pong_fd);
	if (ret < 0) {
		printf("Error: pipe(2)\n");
		exit(1);
	}

	pid = fork();
	if (pid < 0) {
		printf("Error: fork(2)\n");
		exit(1);
	} else if (pid == 0)				// Child process.
		/*
		 * NOTE: In xv6, on a successful call to pipe(2), the file
		 * descriptor located at index 0 is explicitly for reading, and
		 * the file descriptor located at index 1 is explicitly for
		 * writing. Below, there is care taken that each process
		 * (parent + child) each get a READ file descriptor and a WRITE
		 * file descriptor for their respective purposes.
		 */
		child(ping_fd[PIPE_READ_FD_IDX], pong_fd[PIPE_WRITE_FD_IDX]);
	else						// Parent process.
		parent(ping_fd[PIPE_WRITE_FD_IDX], pong_fd[PIPE_READ_FD_IDX]);

	exit(0);
}

/*
 * Child process' function. Read a byte from the parent process, print the
 * ping message, and send the byte back to the parent.
 */
void
child(int ping_fd, int pong_fd)
{
	int ret;
	char byte;

	ret = read(ping_fd, &byte, 1);
	if (ret < 0) {
		printf("<%d> (child) Error: read(2)\n", getpid());
		exit(1);
	}

	printf("<%d> (child): received ping\n", getpid());

	ret = write(pong_fd, &byte, 1);
	if (ret < 0) {
		printf("<%d> (child) Error: write(2)\n", getpid());
		exit(1);
	}
}

/*
 * Parent process' function. Write a byte to the child process, read that same
 * byte back from the parent process, and print the ping message.
 */
void
parent(int ping_fd, int pong_fd)
{
	int ret, status;
	char byte = 'a';

	ret = write(ping_fd, &byte, 1);
	if (ret < 0) {
		printf("<%d> (parent) Error: write(2)\n", getpid());
		exit(1);
	}

	wait(&status);
	if (status == 1)
		exit(1);

	ret = read(pong_fd, &byte, 1);
	if (ret < 0) {
		printf("<%d> (parent) Error: read(2)\n", getpid());
		exit(1);
	}

	printf("<%d> (parent): received pong\n", getpid());
}
