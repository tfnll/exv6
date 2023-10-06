/*
 * Read + write functions for /dev/random device.
 */

#include "../types.h"
#include "../riscv.h"
#include "../spinlock.h"
#include "../sleeplock.h"
#include "../fs.h"
#include "../file.h"
#include "../defs.h"

/*
 * Read from the random device. Reads from the random device produce an infinite
 * stream of random bytes.
 */
int
dev_random_read(struct file *f, int user_dst, uint64 dst, int n)
{
	int ret;
	char *c;
	void *mem;
	/*
	 * Not random, but cyclic through each ASCII character.
	 */
	static char rand = 0;

	if (n > PGSIZE)
		return -1;

	mem = kalloc();
	if (!mem)
		return -1;

	c = (char *) mem;
	for (int i = 0; i < n; c++, i++, rand = ((rand + 1) + 'a') % 'z') {
		*c = rand;
	}

	ret = either_copyout(user_dst, dst, mem, n);
	if (ret < 0)
		return -1;

	kfree(mem);

	return n;
}

/*
 * Write to the random device. Writes to the random device are discarded.
 */
int
dev_random_write(struct file *f, int user_dst, uint64 dst, int n)
{
	return 0;
}

void dev_random_init(void)
{
	devsw[SPECIAL_RANDOM].read = dev_random_read;
	devsw[SPECIAL_RANDOM].write = dev_random_write;
}
