/*
 * Contains the sigalarm() and sigreturn() system calls.
 */

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"

/*
 * If an application calls sigalarm(n, fn), then after every n “ticks” of CPU
 * time that the program consumes, the kernel should cause application function
 * fn to be run. When fn returns, the application should resume where it left
 * off in execution . A tick is a fairly arbitrary unit of time in xv6,
 * determined by how often a hardware timer generates interrupts.
 */
uint64
sys_sigalarm(void)
{
	int n, ticks;
	uint64 fn_addr;
	struct proc *p;

	/*
	 * Fetch the amount of clock ticks before the sigalarm function is
	 * called, as well as a pointer to the sigalarm function.
	 */
	n = argint(0, &ticks);
	if (n < 0)
		return -1;

	n = argaddr(1, &fn_addr);
	if (n < 0)
		return -1;

	p = myproc();
	if (p == 0)
		return -1;

	/*
	 * Cache the clock tick value and sigalarm function. Also, set the
	 * "ticks counter" to 0 and allocate enough space to hold the alarm
	 * trapframe.
	 *
	 * When the sigalarm function is called, register state must be saved so
	 * the process can continue executing where it left off before the
	 * sigalarm function was called. The alarm trapframe stores this
	 * previous register state.
	 *
	 */
	p->sigalarm_ticks = ticks;
	p->sigalarm_fn = fn_addr;
	p->ticks_counter = 0;
	if (!p->alarm_tf) {
		p->alarm_tf = kalloc();
		if (p->alarm_tf == 0)
			panic("alarm_tf kalloc");
	}

	return 0;
}

/*
 * Return executing at the point in which the last sigalarm() was called.
 */
uint64
sys_sigreturn(void)
{
	struct proc *p;

	p = myproc();
	if (p == 0)
		panic("myproc");

	/*
	 * Restore the trapframe to the context in which the process was
	 * executing before the sigalarm function was called.
	 */
	memmove(p->trapframe, p->alarm_tf, sizeof(struct trapframe));

	/*
	 * Indicate that the process is no longer executing inside the sigalarm
	 * function.
	 */
	p->alarm_in_handler = 0;

	return 0;
}
