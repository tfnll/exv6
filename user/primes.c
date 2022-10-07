#include "user/user.h"

/*
 * This is an implementation of Doug McIlroy's Sieve of Eratosthenes simulated
 * by a pipeline of processes.
 *
 * It is further discussed here: https://swtch.com/~rsc/thread/
 * (about halfway down the page).
 *
 * Due to restricted resources on xv6, this implementation will only calculate
 * primes from [2, 31].
 */

#define PRIME_MIN 2
#define PRIME_MAX 31

void sieve();
int fork_child();

int
main(int argc, char *argv[])
{
	int ret, pipe_fd[2], status;

	ret = pipe(pipe_fd);
	if (ret < 0) {
		printf("<%d> Error: pipe(2)\n");
		exit(1);
	}

	ret = fork_child();
	if (ret == 0) {
		close(pipe_fd[1]);
		sieve(pipe_fd[0]);
	}

	close(pipe_fd[0]);

	for (int i = PRIME_MIN; i <= PRIME_MAX; i++)
		write(pipe_fd[1], &i, sizeof(int));

	wait(&status);

	exit(status);
}

void
sieve(int left_fd)
{
	int ret, left, pipeline_next, status, right_fd[2];

	/*
	 * Get a number from the "left" (read) pipe.
	 */
	ret = read(left_fd, &left, sizeof(int));

	/*
	 * Print the number received.
	 */
	printf("prime %d\n", left);

	/*
	 * If we've found the max prime number we set (PRIME_MAX), then exit
	 * with successful return code.
	 */
	if (left == PRIME_MAX)
		exit(0);

	/*
	 * Since PRIME_MAX hasn't yet be found, we will continually receive
	 * other potential primes from the pipeline. Create another right
	 * neighbor in the pipeline to continually pass potential prime numbers
	 * to.
	 */
	ret = pipe(right_fd);

	ret = fork_child();
	if (ret == 0) {
		/*
		 * Child process will only be reading from the left neighbor
		 * (i.e. the parent).
		 */
		close(right_fd[1]);
		sieve(right_fd[0]);
	}

	/*
	 * Parent process will only be writing to the right neighbor.
	 */
	close(right_fd[0]);

	/*
	 * Continually read from the left neighbor and check if the original
	 * "left" divides the number. If it does not, pass it to the next
	 * pipeline stage.
	 */
	for (;;) {
		ret = read(left_fd, &pipeline_next, sizeof(int));
		if (pipeline_next % left != 0)
			write(right_fd[1], &pipeline_next, sizeof(int));

		if (pipeline_next == PRIME_MAX)
			break;
	}

	/*
	 * Wait for child to exit and return child's status code.
	 */
	wait(&status);
	exit(status);
}

int
fork_child()
{
	int ret;

	ret = fork();
	if (ret < 0) {
		printf("<%d> Error: fork(2)\n", getpid());
		exit(1);
	}

	return ret;
}
