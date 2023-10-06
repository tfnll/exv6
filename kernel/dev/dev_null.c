/*
 * Read + write functions for /dev/null device.
 */

#include "../types.h"
#include "../riscv.h"
#include "../spinlock.h"
#include "../sleeplock.h"
#include "../fs.h"
#include "../file.h"
#include "../defs.h"

/*
 * Read from the null device. Reads from the null device always return EOF.
 */
int
dev_null_read(struct file *f, int user_dst, uint64 dst, int n)
{
	return 0;
}

/*
 * Write to the null device. All data written to the null device are discarded.
 */
int
dev_null_write(struct file *f, int user_dst, uint64 dst, int n)
{
	return n;
}

void dev_null_init(void)
{
	devsw[SPECIAL_NULL].read = dev_null_read;
	devsw[SPECIAL_NULL].write = dev_null_write;
}
