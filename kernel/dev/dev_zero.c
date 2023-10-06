/*
 * Read + write functions for /dev/zero device.
 */

#include "../types.h"
#include "../riscv.h"
#include "../spinlock.h"
#include "../sleeplock.h"
#include "../fs.h"
#include "../file.h"
#include "../defs.h"

/*
 * Read from the zero device. Reads from the zero device produce an infinite
 * stream of NULL bytes.
 */
int
dev_zero_read(struct file *f, int user_dst, uint64 dst, int n)
{
	int ret;
	char *c;
	void *mem;

	if (n > PGSIZE)
		return -1;

	mem = kalloc();
	if (!mem)
		return -1;

	c = (char *) mem;
	for (int i = 0; i < n; c++, i++) {
		*c = 0;
	}

	ret = either_copyout(user_dst, dst, mem, n);
	if (ret < 0)
		return -1;

	kfree(mem);

	return n;
}

/*
 * Write to the zero device. Writes to the zero device are discarded.
 */
int
dev_zero_write(struct file *f, int user_dst, uint64 dst, int n)
{
	return n;
}

void dev_zero_init(void)
{
	devsw[SPECIAL_ZERO].read = dev_zero_read;
	devsw[SPECIAL_ZERO].write = dev_zero_write;
}
