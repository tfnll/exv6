	# qemu -kernel loads the kernel at 0x80000000
        # and causes each CPU to jump there.
        # kernel.ld causes the following code to
        # be placed at 0x80000000.
.section .text
.global _entry
_entry:
	# set up a stack for C.
        # stack0 is declared in start.c,
        # with a 4096-byte stack per CPU.
        # sp = stack0 + (hartid * 4096)
	la sp, stack0		# Load location of stack0 (kernel/start.c)

	# Calculate the index into the stack0 array in which the stack this
	# hardware thread will be using is located.
	li a0, 1024*4		# Each stack is 4kB in size.

	csrr a1, mhartid	# Get the integer hardware thread ID for the
				# currently running CPU.

	# Add 1 to the hardware thread ID to "jump" to the stack for the next
	# hardware thread. This is not because we'd like to use that stack, but
	# because in RISC-V, the stack "grows down" (i.e. The stack pointer
	# should begin at the highest address in the stack, and stack memory
	# allocated in decreasing order).
	addi a1, a1, 1		# Increment the hardware ID by 1.

	mul a0, a0, a1		# a0 = 4096 * [hartid + 1] (indicating the top
				# of the hardware thread ID's stack.

	# Set the stack pointer for this hardware thread.
	add sp, sp, a0		# sp = stack0 + (4096 * [hartid + 1])

	# jump to start() in start.c
	call start
spin:
	j spin
