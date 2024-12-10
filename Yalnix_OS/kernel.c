#include "function.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include </clear/courses/comp421/pub/include/comp421/yalnix.h>
#include </clear/courses/comp421/pub/include/comp421/hardware.h>
#include </clear/courses/comp421/pub/include/comp421/loadinfo.h>

/* Handles different system calls based on the input code received in the ExceptionInfo struct.*/
void TrapKernelHandler(ExceptionInfo *info){

    // Handle user's different system call based on the input code
    switch (info->code) {
        case YALNIX_FORK:
            // Handle Fork system call
            info->regs[0] = HandleFork(info);
            TracePrintf(0, "Fork call: Returned (%d)\n", (int) info->regs[0]);
            break;

        case YALNIX_EXEC:
            // Handle Exec system call
            info->regs[0] = HandleExec((char *)info->regs[1], (char **)info->regs[2], info);
            break;

        case YALNIX_EXIT:
            // Handle Exit system call, no return value expected
            HandleExit((int)info->regs[1]);
            break;

        case YALNIX_WAIT:
            // Handle Wait system call
            info->regs[0] = HandleWait((int *)info->regs[1]);
            TracePrintf(0, "Wait call: Returned (%d)\n", (int) info->regs[0]);
            break;

        case YALNIX_GETPID:
            // Handle GetPid system call
            info->regs[0] = HandleGetPid();
            TracePrintf(0, "GetPid call: Returned (%d)\n", (int) info->regs[0]);
            break;

        case YALNIX_BRK:
            // Handle Brk system call
            info->regs[0] = HandleBrk((void *)info->regs[1]);
            TracePrintf(0, "Brk call: Returned (%d)\n", (int) info->regs[0]);
            break;

        case YALNIX_DELAY:
            // Handle Delay system call
            info->regs[0] = HandleDelay((int)info->regs[1]);
            TracePrintf(0, "Delay call: Returned (%d)\n", (int) info->regs[0]);
            break;

        case YALNIX_TTY_READ:
            // Handle TtyRead system call
            info->regs[0] = HandleTtyRead((int)info->regs[1], (void *)info->regs[2], (int)info->regs[3]);
            TracePrintf(0, "TtyRead call: Returned (%d)\n", (int) info->regs[0]);
            break;

        case YALNIX_TTY_WRITE:
            // Handle TtyWrite system call
            info->regs[0] = HandleTtyWrite((int)info->regs[1], (void *)info->regs[2], (int)info->regs[3]);
            TracePrintf(0, "TtyWrite call: Returned (%d)\n", (int) info->regs[0]);
            break;

        default:
            // Unknown system call
            TracePrintf(0, "Received unknown system call\n");
            info->regs[0] = ERROR;
            break;
    }
}

/* Handles the Fork system call.*/
int HandleFork(ExceptionInfo *info) {
    TracePrintf(0, "HandleFork: entered by process (%d)\n", curr_proc->pid);

    (void) info; // Prevent compilation error for unused parameter.

    // Otherwise, initalize PCB struct for new process.
    PCB* child_proc = CreatePCB(curr_proc);
    
    // If PCB was not allocated sucessfully, return ERROR.
    if (child_proc == NULL) {
        return ERROR;
    }
    
    // Copy over user stack and heap pointers to new child process.
    child_proc->uStack_bottom = curr_proc->uStack_bottom;
    child_proc->brk = curr_proc->brk;

    // Hold current processes PID.
    int child_pid = child_proc->pid;

    // Update calling processes child fields.
    enqueueToList(curr_proc->running_children, child_proc);



    /* Here begins the copying process (everything except kernel stack and ctx) */

    // Check if there is enough memory to copy current process.
    int pages_needed = 0;
    unsigned long i;
    for (i = 0; i < PAGE_TABLE_LEN; i++) {
        if (curr_proc->pgt_r0[i].valid == 1) {
            pages_needed++;
        }
    }
    if (pages_needed > free_pframe_count) {
        return ERROR;
    }

    // First set up unused pte to copy into.
    unsigned long pte_for_copy_pfn = USER_STACK_LIMIT >> PAGESHIFT;

    // Next, loop through everything region 0 except kernel stack for curr_proc.
    for (i = MEM_INVALID_PAGES; i < PAGE_TABLE_LEN - KERNEL_STACK_PAGES; i++) {
        // If this is the pte we use to copy-over, then set as invalid because this is not a valid pte idx.
        if (i == pte_for_copy_pfn) {
            child_proc->pgt_r0[i].valid = 0;
            continue;
        }

        // Set valid bit same for corresponding pte in curr and child process.
        child_proc->pgt_r0[i].valid = curr_proc->pgt_r0[i].valid;

        // If valid, we need to copy over.
        if (curr_proc->pgt_r0[i].valid == 1 && i != pte_for_copy_pfn) {
            TracePrintf(0, "HandleFork: Copying over idx (%d)\n", i);

            curr_proc->pgt_r0[pte_for_copy_pfn].valid = 1;
            curr_proc->pgt_r0[pte_for_copy_pfn].uprot = PROT_ALL;
            curr_proc->pgt_r0[pte_for_copy_pfn].kprot = PROT_ALL;

            // Allocate free page. Set transfer pte with same pfn.
            child_proc->pgt_r0[i].pfn = AllocateFreePage();
            curr_proc->pgt_r0[pte_for_copy_pfn].pfn = child_proc->pgt_r0[i].pfn;

            // TLB Flush region 0 page table as we mutate it.
            WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);

            // Copy memory from page in curr_proc to corresponding page in child_proc.
            memcpy((void *) (pte_for_copy_pfn << PAGESHIFT), (void *) (i << PAGESHIFT), PAGESIZE);

            // Set all protections as needed.
            child_proc->pgt_r0[i].uprot = curr_proc->pgt_r0[i].uprot;
            child_proc->pgt_r0[i].kprot = curr_proc->pgt_r0[i].kprot;
        }
    }
    curr_proc->pgt_r0[pte_for_copy_pfn].valid = 0;

    // TLB Flush region 0 page table as we mutate it.
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);



    /* Next, we perform context switch. */

    // First, add child process into list of all processes.
    enqueueToList(processQueue, child_proc);

    // Now, need to copy over kernel stack and saved context. First add curr_proc to ready queue.
    enqueueToList(runningQueue, curr_proc);

    // Context switch to child_proc; copy over kernel stack and ctx in the process.
    curr_proc->needs_copy = 1;
    ContextSwitch(MySwitchFunc, curr_proc->ctx, curr_proc, child_proc);

    // If in child process, return 0. Otherwise if in calling process, return child PID.
    if (curr_proc->pid == child_pid) {
        return 0;
    } else {
        return child_pid;
    }

}

/* Handles the Exec system call.*/
int HandleExec(char *filename, char **argvec, ExceptionInfo *info) {
    TracePrintf(0, "HandleExec: entered by process (%d)\n", curr_proc->pid);

    // Error checking is done in LoadProgram, so we call and process return value.
    int status = LoadProgram(filename, argvec, info);

    // If -1, return ERROR.
    if (status == -1) {
        return ERROR;
    }

    // If -2, must exit.
    if (status == -2) {
        HandleExit(ERROR);
    }

    // Otherwise, no return needed.
    return 0; // Prevent compilation errors.
}

/* Handles the Exit system call.*/
void HandleExit(int status) {
    TracePrintf(0, "HandleExit: entered by process (%d)\n", curr_proc->pid);

    // Set flag that this process is terminated.
    curr_proc->isTerminated = 1;

    // TerminateProcess: handles all updating of orphaned children, parent, and ctx switches.
    TerminateProcess(curr_proc, status);
}

/* Handles the Wait system call.*/
int HandleWait(int *status_ptr) {
    TracePrintf(0, "HandleWait: entered by process (%d)\n", curr_proc->pid);

    // Check if no remaining child processes - return ERROR.
    if (IsLinkedListEmpty(curr_proc->exited_children) && IsLinkedListEmpty(curr_proc->running_children)) {
        return ERROR;
    }

    // If there is children but none that has exited, then add to wait queue then block.
    if (IsLinkedListEmpty(curr_proc->exited_children) == 1) {
        enqueueToList(wait_queue, curr_proc);
        scheduleNextProcess();
    }

    TracePrintf(0, "HandleWait: found exited child of process (%d)\n", curr_proc->pid);

    // We have case for collection of exited child process. First, take status-containing struct for exited child.
    exit_child_status* status_block = (exit_child_status *) dequeueFromList(curr_proc->exited_children);

    // Fill in input parameter status_ptr. Return child's PID.
    unsigned int child_pid = status_block->pid;
    *(status_ptr) = status_block->status;
    free(status_block);

    return child_pid;
}

/* Handles the GetPid system call.*/
int HandleGetPid(void) {
    TracePrintf(0, "HandleGetPid entered by process (%d)\n", curr_proc->pid);

    // Return calling process's (current) PID 
    return curr_proc->pid;
}

/* Adjusts the process's heap boundary to the new address specified by 'addr' */
int HandleBrk(void *addr) {
    TracePrintf(0, "HandleBrk: entered by process (%d)\n", curr_proc->pid);

    // First page that needs to be above heap after new brk.
    unsigned int new_brk_pg = UP_TO_PAGE((unsigned long) addr) >> PAGESHIFT;

    // First page that is outside of heap right now.
    unsigned int curr_first_pg = UP_TO_PAGE(curr_proc->brk) >> PAGESHIFT;

    TracePrintf(0, "HandleBrk: new_brk_pg is (%d) and curr_first_pg is (%d)\n", new_brk_pg, curr_first_pg);

    // Cannot brk if not enough memory is available - overflows into stack,
    // Or, in invalid mem region.
    if (new_brk_pg - 1 >= curr_proc->uStack_bottom - 1 || new_brk_pg - 1 < MEM_INVALID_PAGES || new_brk_pg - curr_first_pg > (unsigned int) free_pframe_count) {
        TracePrintf(0, "HandleBrk: error with handle brk with number of pages (%d)\n", new_brk_pg - curr_first_pg);
        return ERROR;
    }

    // Case 1: Move up brk (ie. addr > current brk)
    if ((unsigned long) addr > curr_proc->brk) {
        TracePrintf(0, "HandleBrk: case 1\n");
        long free_page_pfn;

        // Allocate free physical pages as needed.
        unsigned int i;
        for (i = curr_first_pg; i < new_brk_pg; i++) {
            // Return error if we have no more physical pages left.
            if ((free_page_pfn = AllocateFreePage()) < 0) {
                return ERROR;
            }

            TracePrintf(0, "Allocated pfn (%d) for Brk()\n", free_page_pfn);

            // Update brk position to right above current page.
            curr_proc->brk = (i + 1) << PAGESHIFT;

            // Otherwise, update process' page table as follows.
            curr_proc->pgt_r0[i].pfn = (unsigned int) free_page_pfn;
            curr_proc->pgt_r0[i].valid = 1;
            curr_proc->pgt_r0[i].uprot = (PROT_READ | PROT_WRITE);
            curr_proc->pgt_r0[i].kprot = (PROT_READ | PROT_WRITE);

            // Must flush TBL for R0, since we mutated region 0.
            WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
        }
    }

    // Case 2: Move down brk (ie. new_brk < current brk)
    else if ((unsigned long) addr < curr_proc->brk) {
        TracePrintf(0, "HandleBrk: case 2\n");
        unsigned int i;
        // De-Allocate free physical pages as needed.
        for (i = curr_first_pg - 1; i >= new_brk_pg; i--) {
            // Free the physical page.
            FreePhysicalPage(curr_proc->pgt_r0[i].pfn);

            // Update brk position to right above current page.
            curr_proc->brk = i << PAGESHIFT;

            // Update process' page table as follows.
            curr_proc->pgt_r0[i].valid = 0;

            // Must flush TBL for R0, since we mutated region 0.
            WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
        }

    }  

    // Case 3: (No Code) No change as new brk is same as before.

    (void) curr_first_pg; // Prevent compilation errors.

    // Since successful, return 0.
    return (0);
}

/* Handles the Delay system call.*/
int HandleDelay(int clock_ticks) {
    TracePrintf(0, "HandleDelay: entered by process (%d)\n", curr_proc->pid);

    // Error checking for clock_ticks input
    if (clock_ticks == 0) {
        return 0;
    }
    if (clock_ticks < 0) {
        return ERROR;
    }

    // Now, if clock_ticks is valid, set delay in PCB and add it to delay queue.
    curr_proc->delay_until = total_runningTime + clock_ticks;
    enqueueToList(delay_queue, curr_proc);

    // Schedule next ready function.
    scheduleNextProcess();

    // Return 0 after delay is passed and back to running calling process.
    return 0;
}
/* Handles the TtyRead system call.*/
int HandleTtyRead(int tty_id, void *buf, int len){
    TracePrintf(0, "HandleTtyRead: entered by process (%d)\n", curr_proc->pid);

    // Validate parameters
    if (tty_id < 0 || tty_id >= NUM_TERMINALS || buf == NULL || len < 0) {
        return ERROR;
    }

    // Reading nothing is not an error, so just return
    if (len == 0) {
        return 0;
    }

    // Block the calling process if there is no available input
    if (readReady[tty_id] == -1) {
        enqueueToList(readQueue[tty_id], curr_proc);

        // Schedule next process to run (ContextSwitch happens inside scheduleNextProcess)
        scheduleNextProcess();
    }

    // Came back from ContextSwtich, now we should have the text ready to read
    textStruct* text = peekFromList(inputBuffer[tty_id]);

    // Determine the number of bytes to copy
    int bytesToCopy = (len < text->length) ? len : text->length;

    // Copy the available input to buf up to len bytes
    memcpy(buf, text->line + text->ptr, bytesToCopy);

    // Update the ptr and length for any remaining bytes
    text->ptr += bytesToCopy;
    text->length -= bytesToCopy;

    // If all data has been read from this textStruct
    if (text->length == 0) {
        // Actually dequeue the textStruct from the Buffer list and remove it
        text = dequeueFromList(inputBuffer[tty_id]);
        free(text);
        if (IsLinkedListEmpty(inputBuffer[tty_id])){
            // All text has been read, reset the read-ready flag
            readReady[tty_id] = -1;
        }
    }

    // Return the number of bytes actually copied
    return bytesToCopy;
}

/* Handles the TtyWrite system call.*/
int HandleTtyWrite(int tty_id, void *buf, int len){
    TracePrintf(0, "HandleTtyWrite: entered by process (%d)\n", curr_proc->pid);

     // Validate parameters
    if (tty_id < 0 || tty_id >= NUM_TERMINALS || buf == NULL || len < 0 || len > TERMINAL_MAX_LINE) {
        return ERROR;
    }
    
    // Writing nothing is not an error, so just return
    if (len == 0) {
        return 0;
    }

    // Copy the data from buf into curr_proc->writeRequest (from region 0 to region 1),
    // since ContextSwitch below will invalidate the buf address
    curr_proc->writeRequest = malloc(len * sizeof(char)); 
    if (curr_proc->writeRequest == NULL) {
        fprintf(stderr, "Memory allocation failed! at HandleTtyWrite()\n");
        return ERROR;
    }
    memcpy(curr_proc->writeRequest, buf, len);
    
    // Record the writeLength for later use
    curr_proc->writeLength = len;

    if (writeReady[tty_id] == 1) {
        // If the terminal is ready to be written to, write to hardware
        TracePrintf(0, "HandleTtyWrite: immediately transmitting to terminal\n");

        // Mark the terminal as busy
        writeReady[tty_id] = -1; 
        transmitPCB[tty_id] = curr_proc;
        TtyTransmit(tty_id, curr_proc->writeRequest, len);

    } else {
        TracePrintf(0, "HandleTtyWrite: write not available, scheduling for later\n");

        // Since terminal is not ready, enqueue this process in the write queue
        enqueueToList(writeQueue[tty_id], curr_proc);
    }

    // Schedule next process to run
    scheduleNextProcess();
    
    TracePrintf(0, "HandleTtyWrite: returning len (%d)\n", len);

    // ContextSwitch back from TrapTransmitHandler, successfully write to the Terminal
    return len; 
}
