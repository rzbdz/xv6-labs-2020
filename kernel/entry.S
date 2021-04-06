	# qemu -kernel loads the kernel at 0x80000000
        # and causes each CPU to jump there.
        # kernel.ld causes the following code to
        # be placed at 0x80000000.
.section .text
_entry:
	# set up a stack for C.
        # stack0 is declared in start.c,
        # with a 4096-byte stack per CPU.
        # sp = stack0 + (hartid * 4096)
        la sp, stack0 
        # sp: stack pointer 
        # stack0: char stack0[4096 * NCPU]
        li a0, 1024*4 
        # li: load immediate (load constant, fast)
        # a0: function arg/ret value
	      csrr a1, mhartid
        # mhartid: current thread id
        # mhartid process id
        # csrr: control and status register read, to a1
        addi a1, a1, 1
        # a1 ++
        mul a0, a0, a1
        # a0 = a0 * a1
        add sp, sp, a0
        # stack pointer + a0
        # stack pointer += (1024 * 4 ) * (mhartid + 1)
        # note: the first stack is after hardware devices memory
	# jump to start() in start.c
        call start
  # start do sth in mechine mode, then use ret to supervisor mode
spin:
        j spin
