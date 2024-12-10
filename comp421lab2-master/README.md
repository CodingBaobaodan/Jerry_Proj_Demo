Comp 421 Lab 2:

Group Members: Thomas Lee (dl72) and Jerry Yu (jy151)

Source code (need to compile): helper.c, linked_list.c, yalnix.c, trap.c, kernel.c
Header function (need to include): function.h

Explanation of project:
We construct a Yalnix kernel that can run specified user programs (ie. on command line) that is run on a
simulated computer system hardware (RCS 421). The operation of the kernel begins with a call to KernelStart (in yalnix.c)
that initializes a number of things for the kernel to operate correctly: global variables necessary for running (described in Explanation
of Files, and comments in the code), Trap vector table, region 1 page table and initial (idle) region 0 page table, list of free physical frames,
enable VM, creation of the idle process, then creation of the init process.

Once this is created, all calls/interrupts from the init user process will be handled by the functions in
trap.c and kernel.c (which may call functions in helper.c and linked_list.c to delegate specific functions),
and there are comments written in and for these functions in the provided src files.

When all processes except idle have terminated (ie. exited), on the next interrupt that calls a context
switch from to-be-terminated final process to the idle process, we will Halt() the kernel as otherwise
we would only continue running the idle process (which is only a infinite loop). Some test functions that are
specified in the Test folder include Halt() calls - those for Test/init and Test/console - but these are as
intended for the test.



Explanation of files:
In function.h, we list all the function prototypes listed in all our source files, data structs (including PCB, exited child, write entry, linked list, physical frame list), process queues (including
ones for all processes, delay, wait, ready/running), variables to facilitate allocation of region 0 page tables, free physical frame list, terminal-related data structs, global counters (total program 
running time, pid counter).

In yalnix.c, this contains the initialization of our global variables, code for our KernelStart function, code for helper functions for the KernelStart function
that are delegated a specific part of the initialization process of the kernel, and code for our context switching function (ie. MySwitchFunc).

In trap.c, we handle the Trap/Interrupt handlers for the everything besides TRAP_KERNEL.

In kernel.c, we handle the Trap/Interrupt calls that may be specified from a TRAP_KERNEL interrupt. 

In helper.c, this contains code for LoadProgram (to load in a program from the src directory), code to construct a PCB (divided into idle PCB, and the rest of the PCBs), 
code to allocate a page table for a region 0 (ie. user process), code to allocate/take a free page of physical memory and conversely code to deallocate/free a previously used
page of physical memory.

In linked_list.c, we have helper functions of the LinkedList struct defined in function.h that range from enqueueing, dequeueing, searching for PCB in a LinkedList (assuming it contains
PCB structs), peeking into a list, etc.


How to Run:
Given that the src directory is untouched, simply run the command "make" on the terminal. Then, you can run the kernel as specified in the Lab 2 description (with the additional parameters).
If an init process is not specified, it automatically fills in "init" for the unspecified init process.

