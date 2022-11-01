#include "user/user.h"

int
main(int argc, char *argv[])
{
	int ret;

	ret = uptime();
	if (ret < 0) {
		printf("Error uptime(2)\n");
		exit(1);
	}

	printf("Number of clock tick interrupts since start: %d\n", ret);

	exit(0);
}
