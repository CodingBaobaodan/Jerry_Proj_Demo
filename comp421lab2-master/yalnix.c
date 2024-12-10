#include "function.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include </clear/courses/comp421/pub/include/comp421/yalnix.h>
#include </clear/courses/comp421/pub/include/comp421/hardware.h>
#include </clear/courses/comp421/pub/include/comp421/loadinfo.h>


/* ######################## Global Variable ######################## */

unsigned int pid_counter = 0; // Global counter used to assign unique process IDs (PID)
PCB* curr_proc = NULL;  // Points to the current runnning process
int is_half_used = 0; // Flag variable to indicate if region 1 page table page is half used

LinkedList* processQueue = NULL; // List of all processes 
LinkedList* runningQueue = NULL; // FIFO queue for all ready-running processes
LinkedList* delay_queue = NULL; // FIFO queue for all delayed processes
LinkedList* wait_queue = NULL; // FIFO queue for all waiting processes


InterruptHandler *interruptVectorTable = NULL;
void *kernel_brk = NULL; // Initial kernel break location
int vm_enabled = 0; // Flag to indicate if VM is enabled

// Page Tables
struct pte *pgt_r0 = NULL;
struct pte *pgt_r1 = NULL;
unsigned long addr_next_pgt_r0 = VMEM_1_LIMIT - PAGESIZE;
unsigned long curr_pgt_paddr = 0;

// Idle process's PCB
PCB* idle_pcb = NULL;

// Total program running time.
unsigned long total_runningTime = 0;

// Physical frame head, and count.
pframe *free_pframe_head = NULL;
int free_pframe_count = 0;

// Terminal related Data Structure
LinkedList* inputBuffer[NUM_TERMINALS] = {NULL}; // Input buffer read for each terminal 
int readReady[NUM_TERMINALS] = {0}; // Flag to indicate if terminal i has text available to read, -1 means not ready. 1 means ready.
int writeReady[NUM_TERMINALS] = {0}; // Flag to indicate if terminal i is ready to be written, -1 means not ready. 1 means ready.
LinkedList* readQueue[NUM_TERMINALS] = {NULL}; // Queue that stores the process's PCB for a read request on terminal i
LinkedList* writeQueue[NUM_TERMINALS] = {NULL};// Queue that stores the process's PCB for a write request on terminal i
PCB* transmitPCB[NUM_TERMINALS] = {NULL};

/* ######################## Global Variable ######################## */


/* Initialize necessary data structure for the kernel */
void initKernel(void){
    TracePrintf(0, "Starting initKernel\n");

    processQueue = CreateLinkedList();
    runningQueue = CreateLinkedList();
    delay_queue = CreateLinkedList();
    wait_queue = CreateLinkedList();

    // If we cannot initialize the kernel, we must halt this process.
    if (processQueue == NULL || runningQueue == NULL || delay_queue == NULL || wait_queue == NULL) {
        TracePrintf(0, "Cannot initialize kernel; halting process.\n");
        printf("Cannot initialize kernel; halting process.\n");
        Halt();
    }

    int i;
    for (i = 0; i < NUM_TERMINALS; i++){
        inputBuffer[i] = CreateLinkedList();
        readQueue[i] = CreateLinkedList();
        writeQueue[i] = CreateLinkedList();

        // If we cannot initialize the kernel, we must halt this process.
        if (inputBuffer[i] == NULL || readQueue[i] == NULL || writeQueue[i] == NULL) {
            TracePrintf(0, "Cannot initialize kernel; halting process.\n");
            printf("Cannot initialize kernel; halting process.\n");
            Halt();
        }

        readReady[i] = -1; // Initially terminal[i] is not ready to be read
        writeReady[i] = 1; // Initially terminal[i] is ready to write on
        transmitPCB[i] = NULL;
    }

    TracePrintf(0, "Finished initKernel\n");
}

/* Initalizes the interrupt vector table, and writes its memory location to the REGISTER. */
void initInterruptVector(void) {

    TracePrintf(0, "Starting initInterruptVector\n");

    // allocate memory for the interrupt vector table in the region1 heap
    // "table" is a pointer to an array of function pointers
    interruptVectorTable = (InterruptHandler *)malloc(TRAP_VECTOR_SIZE * sizeof(InterruptHandler));
    
    // If we cannot initialize the kernel, we must halt this process.
    if (interruptVectorTable == NULL) {
        TracePrintf(0, "Cannot initialize kernel; halting process.\n");
        printf("Cannot initialize kernel; halting process.\n");
        Halt();
    }

    // Initialize all entries to NULL
    int i;
    for (i = 0; i < TRAP_VECTOR_SIZE; i++) {
        interruptVectorTable[i] = NULL;
    }

    // Assign handler functions to specific entries
    interruptVectorTable[TRAP_KERNEL] = TrapKernelHandler;
    interruptVectorTable[TRAP_CLOCK] = TrapClockHandler;
    interruptVectorTable[TRAP_ILLEGAL] = TrapIllegalHandler;
    interruptVectorTable[TRAP_MEMORY] = TrapMemoryHandler;
    interruptVectorTable[TRAP_MATH] = TrapMathHandler;
    interruptVectorTable[TRAP_TTY_RECEIVE] = TrapReceiveHandler;
    interruptVectorTable[TRAP_TTY_TRANSMIT] = TrapTransmitHandler;

    TracePrintf(0, "Finished initInterruptVector\n");
}

/*

 Initializes the list of free physical pages, and the page tables
 for region 0 and 1. Writes addresses to appropriate registers. 
 
 */
void InitMemoryManagement(unsigned int pmem_size)
{   
    TracePrintf(0, "Starting InitMemoryManagement\n");

    // Before dividing physical memory into frames, make sure to allocate memory for page tables.
    pgt_r0 = (struct pte*) malloc(PAGE_TABLE_SIZE);
    pgt_r1 = (struct pte*) malloc(PAGE_TABLE_SIZE);

    // If we cannot initialize the kernel, we must halt this process.
    if (pgt_r0 == NULL || pgt_r1 == NULL) {
        TracePrintf(0, "Cannot initialize kernel; halting process.\n");
        printf("Cannot initialize kernel; halting process.\n");
        Halt();
    }

    unsigned long i;    

    // Fill in list structure for physical frames, and remove non-free frames in use by the kernel.
    pframe *temp;
    unsigned long frame_cnt = 0;

    // Have to do it in 2 passes due to calling internal malloc.
    for (i = PMEM_BASE; i < PMEM_BASE + pmem_size; i += PAGESIZE) {
        if (i == PMEM_BASE) {
            free_pframe_head = (pframe *) malloc(sizeof(pframe));

            // If we cannot initialize the kernel, we must halt this process.
            if (free_pframe_head == NULL) {
                TracePrintf(0, "Cannot initialize kernel; halting process.\n");
                printf("Cannot initialize kernel; halting process.\n");
                Halt();
            }

            temp = free_pframe_head;
            temp->frame_num = frame_cnt++;
        } else {
            temp->next = (pframe *) malloc(sizeof(pframe));

            // If we cannot initialize the kernel, we must halt this process.
            if (temp->next == NULL) {
                TracePrintf(0, "Cannot initialize kernel; halting process.\n");
                printf("Cannot initialize kernel; halting process.\n");
                Halt();
            }

            temp = temp->next;
            temp->frame_num = frame_cnt++;
            free_pframe_count++;
        }   
    }

    temp = free_pframe_head;
    pframe *temp2;
    while (temp != NULL) {
        if (temp->frame_num >= (KERNEL_STACK_BASE >> PAGESHIFT) && temp->frame_num < (((unsigned long) kernel_brk) >> PAGESHIFT) ) {
            temp2 = temp;
            temp = temp->next;
            free(temp2);
            free_pframe_count--;
        } else {
            temp = temp->next;
        }
    }

    TracePrintf(0, "InitMemoryManagement: number of free frames is (%d)\n", free_pframe_count);

    unsigned int pg_num;

    // Fill in page table (region 0).
    for (i = KERNEL_STACK_BASE; i < KERNEL_STACK_LIMIT; i += PAGESIZE) {
        pg_num = i >> PAGESHIFT;
        pgt_r0[pg_num].valid = 1;
        pgt_r0[pg_num].uprot = PROT_NONE;
        pgt_r0[pg_num].kprot = (PROT_READ | PROT_WRITE);
        pgt_r0[pg_num].pfn = pg_num;
    }

    WriteRegister(REG_PTR0, (RCS421RegVal) pgt_r0);


    // Fill in page table (region 1).
    addr_next_pgt_r0 = (unsigned long) addr_next_pgt_r0;

    for (i = VMEM_1_BASE; i < (unsigned long) kernel_brk; i += PAGESIZE) {
        pg_num = (i - VMEM_1_BASE) >> PAGESHIFT;
        pgt_r1[pg_num].valid = 1;
        pgt_r1[pg_num].uprot = PROT_NONE;
        pgt_r1[pg_num].pfn = i >> PAGESHIFT;
        
        if (i < (unsigned long) &_etext) {
            pgt_r1[pg_num].kprot = (PROT_READ | PROT_EXEC);
        } else {
            pgt_r1[pg_num].kprot = (PROT_READ | PROT_WRITE);
        }
    }

    WriteRegister(REG_PTR1, (RCS421RegVal) pgt_r1);

    TracePrintf(0, "Finished InitMemoryManagement\n");
}

/* 
 * Upon a malloc call that needs more memory allocated in kernel heap, 
 * will allocate memory and shift the kernel_brk global variable to new
 * brk location. Returns -1 on error, and 0 on success. Split into before and
 * after VM enabled sections.
 */
int SetKernelBrk(void *addr) {
    TracePrintf(0, "SetKernelBrk: attempting to set new kernel brk.\n");
    if (!vm_enabled) {
        // Check if new address overflows past region 1 (into page table region 0)
        if ((unsigned long) addr > addr_next_pgt_r0) {
            TracePrintf(0, "SetKernelBrk: before VM - new addr is not valid (overflows).\n");
            return -1;
        }

        TracePrintf(0, "SetKernelBrk: set kernel break (before VM) to addr (0x%lx).\n", (unsigned long) addr);

        // Before VM is enabled, just update the break location
        kernel_brk = addr;

    } else {
        TracePrintf(0, "SetKernelBrk: trying to set kernel break (after VM).\n");
        // After VM is enabled, allocate and map physical memory to the new break address
        // This part is more complex and involves interacting with the VM system

        // First check if we have pages left to allocate memory.
        if ((unsigned long) addr - UP_TO_PAGE((unsigned long) kernel_brk) > (unsigned long) (free_pframe_count << PAGESHIFT)) {
            TracePrintf(0, "SetKernelBrk: after VM - not enough pages to allocate memory.\n");
            return -1;
        }

        // Next check if the new kernel break will overflow past region 1. (into page table region 0)
        if ((unsigned long) addr > addr_next_pgt_r0) {
            TracePrintf(0, "SetKernelBrk: after VM - new addr is not valid (overflows).\n");
            return -1;
        }

        // Otherwise, we can attempt to allocate memory and set a new kernel break
        // Case 1: Move up brk (ie. addr > current brk)
        if ((unsigned long) addr > (unsigned long) kernel_brk) {

            // Allocate free physical pages as needed.
            unsigned int i;

            // Compute what is the last page table idx to fill, and how many new that is.
            unsigned int last_pg_idx_to_fill = (DOWN_TO_PAGE((unsigned long) addr) - VMEM_1_BASE) >> PAGESHIFT;
            unsigned int first_pg_idx_to_fill = (UP_TO_PAGE((unsigned long)kernel_brk) - VMEM_1_BASE) >> PAGESHIFT;

            TracePrintf(0, "SetKernelBrk: attempting to allocate pages from page idx (%d) to idx (%d).\n", first_pg_idx_to_fill, last_pg_idx_to_fill);

            long free_page_pfn;
            for (i = first_pg_idx_to_fill; i <= last_pg_idx_to_fill; i++) {

                // Only allocate if there is no page already allocated
                if (pgt_r1[i].valid == 0) {
                    TracePrintf(0, "SetKernelBrk: allocating idx (%d) in page table region 1.\n", i);

                    // Return error if we have no more physical pages left.
                    if ((free_page_pfn = AllocateFreePage()) < 0) {
                        return ERROR;
                    }

                    // Otherwise, update kernel's region 1 page table as follows.
                    pgt_r1[i].pfn = (unsigned int) free_page_pfn;
                    pgt_r1[i].valid = 1;
                    pgt_r1[i].uprot = PROT_NONE;
                    pgt_r1[i].kprot = (PROT_READ | PROT_WRITE);

                    // Must flush TBL for R1, since we mutated region 1.
                    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1);
                }
            }

            TracePrintf(0, "SetKernelBrk: set kernel break (after VM) to addr (0x%lx).\n", (unsigned long) addr);

            // Lastly, update the kernel_brk to new brk addr
            kernel_brk = addr;
        }

        // Case 2: Break is lower than current kernel_brk so we do nothing (as specified in instructions)

    }
    return 0; // Indicate success
}


/*
 * Creates idle process. Sets up PCB and sets it as current process.
 */
void
CreateIdleProcess(ExceptionInfo *info, char **cmd_args)
{
    TracePrintf(0, "Now creating idle process.\n");

    // Builds PCB structure for idle process.
    idle_pcb = CreateIdlePCB(NULL);

    // Sets idle process and current process, and loads it into memory.
    curr_proc = idle_pcb;
    LoadProgram("idle", cmd_args, info);

    //enqueueToList(processQueue, idle_pcb);
    //enqueueToList(runningQueue, idle_pcb);
    TracePrintf(0, "Successfully loaded in idle process.\n");
}

/*
 * Creates init process. Sets up PCB and sets it as current process.
 */
void
CreateInitProcess(ExceptionInfo *info, char **cmd_args)
{

    // Create PCB structure for init process.
    struct PCB* init_pcb = CreatePCB(idle_pcb);
    enqueueToList(idle_pcb->running_children, init_pcb);
    TracePrintf(0, "Now creating init process with pid (%d).\n", init_pcb->pid);

    // Context switch from idle process to init process.
    idle_pcb->needs_copy = 1;
    ContextSwitch(MySwitchFunc, curr_proc->ctx, curr_proc, init_pcb);

    TracePrintf(0, "Now in process (%d).\n", curr_proc->pid);

    // If were in the init process, then we load it into memory.
    if (curr_proc->pid == 1) {
        TracePrintf(0, "We are in init process, trying to load in.\n");

        // First, check if we have an init program on the cmdargs
        int num_cmdargs = 0;
        int i = 0;
        for (; cmd_args[i] != NULL; i++) {
	        num_cmdargs++;
        }

        // Run the specified init program, if we have it.
        if (num_cmdargs > 0) {
            LoadProgram(cmd_args[0], cmd_args, info); // cmd_args[0] should be name of init process
        }
        // Otherwise, run a default "init" program.
        else {
            LoadProgram("init", cmd_args, info);
        }

        enqueueToList(processQueue, curr_proc);
    }

    TracePrintf(0, "CreateInitProcess: at end.\n");
}

/* Main procedure to initialize the kernel and start the idle and init processes. */
void KernelStart(ExceptionInfo *info, unsigned int pmem_size, void *orig_brk, char **cmd_args) {

    // Set iniital kernel brk as orig_brk.
    kernel_brk = orig_brk;

    // Initalize kernel data structure
    initKernel();

    // Initialize the interrupt vector table
    initInterruptVector();

    // place the starting address of interruptVectorTable in REG_VECTOR_BASE register
    WriteRegister(REG_VECTOR_BASE, (RCS421RegVal)interruptVectorTable);

    // Initialize memory management, taking into account the physical memory size
    InitMemoryManagement(pmem_size);

    // Enable virtual memory
    WriteRegister(REG_VM_ENABLE, 1);
    // Set flag for vm
    vm_enabled = 1;

    // Create the idle process, which should run when no other processes are ready
    CreateIdleProcess(info, cmd_args);

    // Create the init process, which is the first user process to run
    CreateInitProcess(info, cmd_args);

}

/* 
 * Helper function to context switch. p1 is current process; p2
 * is process that we want to switch to.
 */
SavedContext
*MySwitchFunc(SavedContext *ctxp, void *p1, void* p2)
{   
    struct PCB *pcb1 = (PCB *) p1;
    struct PCB *pcb2 = (PCB *) p2;
    unsigned long i;

    (void) ctxp; // Prevent compilation error for unused parameter.
     
    TracePrintf(0, "Entered MySwitchFunc for process ID %d\n", curr_proc->pid);
    TracePrintf(0, "Want to switch to process ID %d\n", pcb2->pid);

    // Cases for if we need to copy only kernel stack and saved context. Fork or init/idle.
    if (pcb1->needs_copy == 1) {
        TracePrintf(0, "MySwitchFunc copying over kernel stack and saved context\n");
        // Now, we need to copy kernel stack.
        // First set up unused pte to copy into.
        unsigned long pte_for_copy_pfn = USER_STACK_LIMIT >> PAGESHIFT;

        // Next, loop through everything region 0 kernel stack in p1.
        unsigned long page_num;
        for (i = KERNEL_STACK_BASE; i < KERNEL_STACK_LIMIT; i += PAGESIZE) {
            page_num = i >> PAGESHIFT;

            // Set valid bit same for corresponding pte in curr and child process.
            pcb2->pgt_r0[page_num].valid = pcb1->pgt_r0[page_num].valid;

            // If valid, we need to copy over.
            if (pcb1->pgt_r0[page_num].valid == 1 && page_num != pte_for_copy_pfn) {
                TracePrintf(0, "Now copying over idx (%d) at addr (0x%lx).\n", page_num, i);

                pcb1->pgt_r0[pte_for_copy_pfn].valid = 1;
                pcb1->pgt_r0[pte_for_copy_pfn].uprot = PROT_ALL;
                pcb1->pgt_r0[pte_for_copy_pfn].kprot = PROT_ALL;

                // Allocate free page. Set transfer pte with same pfn.
                pcb2->pgt_r0[page_num].pfn = AllocateFreePage();
                pcb1->pgt_r0[pte_for_copy_pfn].pfn = pcb2->pgt_r0[page_num].pfn;

                TracePrintf(0, "Allocated pfn (%d) for copying kernel stack\n", pcb2->pgt_r0[page_num].pfn);

                // TLB Flush region 0 page table as we mutate it.
                WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);

                // Copy memory from page in curr_proc to corresponding page in child_proc.
                memcpy((void *) (pte_for_copy_pfn << PAGESHIFT), (void *) (i), PAGESIZE);

                TracePrintf(0, "Valid bit at idx (%d) for pcb2 is (%d).\n", page_num, pcb2->pgt_r0[page_num].valid);

                // Set all protections as needed.
                pcb2->pgt_r0[page_num].uprot = pcb1->pgt_r0[page_num].uprot;
                pcb2->pgt_r0[page_num].kprot = pcb1->pgt_r0[page_num].kprot;
            }
        }

        TracePrintf(0, "Finished copying over.\n");
        
        // Don't forget to set to invalid.
        pcb1->pgt_r0[pte_for_copy_pfn].valid = 0;

        // TLB Flush region 0 page table as we mutate it.
        WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);

        // A trace print to help.
        TracePrintf(0, "Pcb2 page table is at physical address (0x%lx).\n", pcb2->pgt_r0_paddr);

        // Write new page table for p2 to register.
        WriteRegister(REG_PTR0, (RCS421RegVal) (pcb2->pgt_r0_paddr));

        TracePrintf(0, "Flushing TLB.\n");

        WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);

        TracePrintf(0, "Done flushing TLB.\n");

        // Then, we copy over the SavedContext field in the PCB.
        memcpy(pcb2->ctx, pcb1->ctx, sizeof(SavedContext));

        // Set needs_copy bit in calling process.
        pcb1->needs_copy = -1;

        // Set running process as p2 and return its ctx.
        curr_proc = pcb2;

        TracePrintf(0, "Done Context Switch.\n");

        return (pcb2->ctx);
    } 
    // Case for when p1 is terminated.
    else if (pcb1->isTerminated == 1) {
        TracePrintf(0, "MySwitchFunc terminating process\n");

        // First, deallocate every physical frame that was used in region 0 mem.
        for (i = MEM_INVALID_PAGES; i < PAGE_TABLE_LEN; i++) {
            if (pcb1->pgt_r0[i].valid == 1) {
                FreePhysicalPage(pcb1->pgt_r0[i].pfn);
                pcb1->pgt_r0[i].valid = 0;
            }
        }

        // Remove terminated PCB from process queue.
        if (SearchAndRemovePCB(processQueue, pcb1->pid) == ERROR) {
            TracePrintf(0, "Could not succesfully remove PCB from process queue\n");
        }

        // Before we write to register, if we are terminating the last process, we don't write to register.
        if (pcb2 == idle_pcb && IsLinkedListEmpty(processQueue) == 1) {
            printf("All processes (except idle) have been exited. Now Halting the kernel.\n");
            Halt(); // Instesad, we Halt to stop execution.
        }

        // Rewrite new page table into register. Flush TLB for region 0.
        WriteRegister(REG_PTR0, (RCS421RegVal) (pcb2->pgt_r0_paddr));
        WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);

        // Free the rest of PCB for process 1.
        freeListContents(pcb1->running_children);
        freeListContentsExitChildren(pcb1->exited_children);
        free(pcb1->ctx);
        free(pcb1);

        // Set running process as p2 and return its ctx.
        curr_proc = pcb2;

        TracePrintf(0, "Done Context Switch.\n");

        return (pcb2->ctx);
    } 
    // Case for when we have a "normal" context switch from p1 to p2.
    TracePrintf(0, "MySwitchFunc performing 'normal' context switch\n");
    
    WriteRegister(REG_PTR0, (RCS421RegVal) (pcb2->pgt_r0_paddr));
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);

    // Set running process as p2 and return its ctx.
    curr_proc = pcb2;

    TracePrintf(0, "Done Context Switch.\n");

    return (pcb2->ctx);
}