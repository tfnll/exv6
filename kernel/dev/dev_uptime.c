/*
 * Read + write functions for /dev/uptime device.
 */

#include "../types.h"
#include "../riscv.h"
#include "../spinlock.h"
#include "../sleeplock.h"
#include "../fs.h"
#include "../file.h"
#include "../defs.h"

extern uint64 sys_uptime(void);

/*
 * Read from the uptime device. Reads from the uptime device fill the buffer
 * with the number of clock ticks since boot.
 */
int
dev_uptime_read(struct file *f, int user_dst, uint64 dst, int n)
{
	int ret;
	char *str_buf;
	uint64 time;

	str_buf = (char *) kalloc();
	if (!str_buf)
		return -1;

	time = sys_uptime();
	itoa((int) time, str_buf, 10);

	ret = either_copyout(user_dst, dst, (void *) str_buf, strlen(str_buf));
	if (ret < 0)
		return -1;

	kfree((void *) str_buf);

	return strlen(str_buf) + 1;
}

/*
 * Write to the uptime device. Writes to the uptime device are discarded.
 */
int
dev_uptime_write(struct file *f, int user_dst, uint64 dst, int n)
{
	return n;
}

void dev_uptime_init(void)
{
	devsw[SPECIAL_UPTIME].read = dev_uptime_read;
	devsw[SPECIAL_UPTIME].write = dev_uptime_write;
}
