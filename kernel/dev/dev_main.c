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
 * Initialize the special devices.
 */
void dev_special_init(void)
{
	dev_null_init();	/* /dev/null	*/
	dev_random_init();	/* /dev/random	*/
	dev_uptime_init();	/* /dev/uptime	*/
	dev_zero_init();	/* /dev/zero	*/
}
