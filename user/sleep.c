#include "user/user.h"

int
main(int argc, char *argv[])
{
	int ret, nticks;

	if (argc != 2) {
		printf("Usage: %s $NUM_TICKS\n", argv[0]);
		exit(1);
	}

	nticks = atoi(argv[1]);
	if (nticks == 0)
		exit(0);
	else if (nticks < 0) {
		printf("Error: Cannot sleep for %d ticks\n", nticks);
		exit(1);
	}

	ret = sleep(nticks);
	if (ret < 0) {
		printf("Error: sleep(2)\n");
		exit(1);
	}

	exit(0);
}
