# Initial process that execs /init.
# This code runs in user space.

#include "syscall.h"

# exec(init, argv)
.globl start
start:
        # path of init
        la a0, init
        # args
        la a1, argv
        # load exec code to a7: function argument (last one)
        li a7, SYS_exec
        # environment call
        # A process make a syscall by exec ecall
        # raises the hardware privilege level and 
        # changes the program counter to a kernel-defined entry point.
        # code at the entry point switches to a kernel stack and 
        # executes the kernel instructions that implement the system call. 
        ecall

# for(;;) exit();
exit:
        li a7, SYS_exit
        ecall
        jal exit

# char init[] = "/init\0";
init:
  .string "/init\0"

# char *argv[] = { init, 0 };
.p2align 2
argv:
  .long init
  .long 0
