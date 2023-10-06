/*
 * xv6 Symbolic links.
 */

#include "types.h"
#include "riscv.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "stat.h"
#include "defs.h"

uint64
sys_symlink(void)
{
	int ret, len;
	char target[MAXPATH], link[MAXPATH];
	struct inode *ip;

	ret = argstr(0, target, MAXPATH);
	if (ret < 0)
		panic("symlink: unable to fetch target path");

	ret = argstr(1, link, MAXPATH);
	if (ret < 0)
		panic("symlink: unable to fetch link path");

	begin_op();

	/*
	 * Create an inode for the symbolic link file.
	 */
	ip = create(link, T_SYMLINK, 0, 0);
	if (!ip) {
		/*
		 * inode creation failed.
		 */
		end_op();
		return -1;
	}

	/*
	 * Write two data points (concurrently) to the inode's data blocks.
	 *	1) The length of the target's path.
	 *	2) The target's path.
	 */
	len = strlen(target);
	writei(ip, 0, (uint64) &len, 0, sizeof(int));
	writei(ip, 0, (uint64) target, sizeof(int), len + 1);

	iupdate(ip);
	iunlockput(ip);

	end_op();

	return 0;
}
