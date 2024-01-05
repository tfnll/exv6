#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

void main();
void timerinit();

// entry.S needs one stack per CPU.
__attribute__ ((aligned (16))) char stack0[4096 * NCPU];

// A scratch area per CPU for machine-mode timer interrupts.
uint64 timer_scratch[NCPU][5];

// Assembly code in kernelvec.S for machine-mode timer interrupt.
extern void timervec();

// entry.S jumps here in machine mode on stack0.
void
start()
{
	unsigned long mstatus;
  /*
   * When this function is entered, the CPU is in machine mode. start()
   * performs some basic configuration before switching the CPU to supervisor
   * mode and jumping to main(). The mret instruction is used to switch to
   * supervisor mode, and uses the machine registers to guide its execution.
   *
   * Some tasks that are performed below:
   *	1) mret views the mstatus register to understand which mode it is
   *       returning to. Therefore, the bitmask indicating supervisor mode must
   *	   written to the mstatus register.
   *
   *	2) To view where the CPU should jump to on mret, the Machine Exception
   *	   Program Counter register (mepc) is viewed. Set the mepc register to
   *	   point to main()'s address.
   *
   *	3) Disable virtual address translation in supervisor mode by writing 0
   *	   into the page-table register satp.
   *
   *	4) Delegate all interrupts and exceptions to supervisor mode.
   *
   *	5) Give supervisor mode access to all of physical memory. The kernel
   *	   is able to use the entire physical memory for its management needs.
   *
   *	6) Program the clock chip to generate timer interrupts.
   *
   *	7) Store the hardware thread's ID in it's tp register.
   */

  /*
   * Set M Previous Privilege mode to Supervisor. When mret is executed, the
   * processor will switch into supervisor mode.
   */
  mstatus = r_mstatus();
  mstatus &= ~MSTATUS_MPP_MASK; // Clear the previous privilege mode bit(s).
  mstatus |= MSTATUS_MPP_S;	// Set the MPP mode to supervisor.
  w_mstatus(mstatus);		// Write new MPP mode (supervisor) to mstatus.

  /*
   * Set M Exception Program Counter to main. When mret is executed, the
   * processor will jump to main()'s address, effectively beginning to run main
   * in superviosr mode.
   * requires gcc -mcmodel=medany
   */
  w_mepc((uint64)main);

  /*
   * Disable paging for now. The satp register holds the address of the
   * supervisor mode page table. Writing 0 to this register effectively disables
   * paging in supervisor mode.
   */
  w_satp(0);

  /*
   * Delegate all interrupts and exceptions to supervisor mode. The medeleg and
   * mideleg registers hold bitflags indicating which exception or interrupt
   * will be handled by supervisor mode. Since these registers are 32 bits in
   * size, setting their values to 0xffff effectively indicates that all
   * exceptions and interrupts will be handled in supervisor mode.
   *
   * The sie register holds interrupt-enable bits. There are three types of
   * interrupts:
   * - Software interrupt (SSIE)
   * - Timer interrupt (STIE)
   * - External interrupt (SEIE)
   *
   * By setting each of the interrupt's bitflag in the sie register, each type
   * of interrupt will be enabled in supervisor mode.
   */
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);

  /*
   * Configure Physical Memory Protection to give supervisor mode access to all
   * of physical memory.
   *
   * The PMP address registers are 16 CSRs named pmpaddr0-pmpaddr15. Each PMP
   * address register encodes bits 52-55 of a 56-bit physical address. Not all
   * physical address bits may be implemented, and so the pmpaddr registers are
   * WARL.
   *
   * TODO: How do the PMP address and PMP configuration registers work together?
   */
  w_pmpaddr0(0x3fffffffffffffull);
  w_pmpcfg0(0xf);

  /*
   * Ask for clock interrupts.
   */
  timerinit();

  /*
   * Keep each CPU's hartid in its tp register, for cpuid().
   */
  int id = r_mhartid();
  w_tp(id);

  // switch to supervisor mode and jump to main().
  asm volatile("mret");
}

/*
 * Set up the current CPU to receive timer interrupts in machine mode, which
 * arrive at timervec in kernelvec.S, which in-turn turns them into software
 * interrupts for devintr() in trap.c.
 */
void
timerinit()
{
  // Each CPU has a separate source of timer interrupts.
  int id = r_mhartid();

  /*
   * Ask the CLINT for a timer interrupt.
   *
   * Platforms provide a real-time counter, exposed as a memory-mapped
   * machine-mode register, mtime. Platforms also provide a 64-bit memory-mapped
   * machine-mode timer compare register, mtimecmp, which causes a timer
   * interrupt to be posted when the mtime register contains a value greater
   * than or equal to the value in the mtimecmp register.
   *
   * Set this CPU's mtimecmp register to to be 1000000 cycles more than the
   * current global mtime register, in-effect setting the timer to interrupt
   * every 1000000 cycles.
   */
  int interval = 1000000;	// Cycles (about 1/10th second in qemu).
  *(uint64 *) CLINT_MTIMECMP(id) = *(uint64 *) CLINT_MTIME + interval;

  /*
   * Prepare information in scratch[] for timervec.
   * scratch[0..2]: Space for timervec to save registers.
   * scratch[3]: Address of CLINT MTIMECMP register.
   * scratch[4]: Desired interval (in cycles) between timer interrupts.
   */
  uint64 *scratch = &timer_scratch[id][0];
  scratch[3] = CLINT_MTIMECMP(id);
  scratch[4] = interval;
  w_mscratch((uint64) scratch);

  // Set the machine-mode trap handler.
  w_mtvec((uint64) timervec);

  // Enable machine-mode interrupts.
  w_mstatus(r_mstatus() | MSTATUS_MIE);

  // Enable machine-mode timer interrupts.
  w_mie(r_mie() | MIE_MTIE);
}
