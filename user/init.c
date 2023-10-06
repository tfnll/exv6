// init: The initial user-level program

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

static int dev_files_init(void);

char *argv[] = { "sh", 0 };

int
main(void)
{
  int pid, wpid;

  if(open("console", O_RDWR) < 0){
    mknod("console", 1, 1);
    open("console", O_RDWR);
  }
  dup(0);  // stdout
  dup(0);  // stderr

  if (dev_files_init() < 0) {
	printf("init: Error creating special device files\n");
	exit(1);
  }

  for(;;){
    printf("init: starting sh\n");
    pid = fork();
    if(pid < 0){
      printf("init: fork failed\n");
      exit(1);
    }
    if(pid == 0){
      exec("sh", argv);
      printf("init: exec sh failed\n");
      exit(1);
    }

    for(;;){
      // this call to wait() returns if the shell exits,
      // or if a parentless process exits.
      wpid = wait((int *) 0);
      if(wpid == pid){
        // the shell exited; restart it.
        break;
      } else if(wpid < 0){
        printf("init: wait returned an error\n");
        exit(1);
      } else {
        // it was a parentless process; do nothing.
      }
    }
  }
}

static int
dev_files_init(void)
{
	if (mkdir("/dev") != 0)
		return -1;

	if (mknod("/dev/null", 2, 0) != 0)
		return -1;

	if (mknod("/dev/zero", 3, 0) != 0)
		return -1;

	if (mknod("/dev/random", 4, 0) != 0)
		return -1;

	if (mknod("/dev/uptime", 5, 0) != 0)
		return -1;

	return 0;
}
