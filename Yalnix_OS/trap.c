#include "function.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include </clear/courses/comp421/pub/include/comp421/yalnix.h>
#include </clear/courses/comp421/pub/include/comp421/hardware.h>
#include </clear/courses/comp421/pub/include/comp421/loadinfo.h>

/*
 * Manages clock interrupts to update process times, handle delayed processes,
 * and potentially trigger context switches for round-robin scheduling.
 */
void TrapClockHandler(ExceptionInfo *info) {
    // This handler is invoked on every clock interrupt.
    TracePrintf(0, "TrapClockHandler: entered by process (%d).\n", curr_proc->pid);
    
    // Increment the running time of the current process.
    curr_proc->runningTime += 1;
    total_runningTime++;

    /* First check delay queue to see if there is any process to switch to. */
    if (IsLinkedListEmpty(delay_queue) != 1) {
        TracePrintf(0, "TrapClockHandler: looking through elements in delay queue.\n");
        ListNode* temp = delay_queue->head;
        ListNode* temp2;
        PCB* toRemove;
        while (temp != NULL) {
            TracePrintf(0, "TrapClockHandler: looking at PCB pid (%d) with delay (%d).\n", 
            ((PCB*) temp->data)->pid, ((PCB*) temp->data)->delay_until);

            if (total_runningTime > ((PCB*) temp->data)->delay_until) {
                // If delay is over for one of the processes, then remove it from delay queue.
                temp2 = temp->next;
                toRemove = SearchAndReturnPCB(delay_queue, ((PCB*) temp->data)->pid);
                SearchAndRemovePCB(delay_queue, ((PCB*) temp->data)->pid);

                // Add it to the ready/running queue.
                enqueueToList(runningQueue, toRemove);

                TracePrintf(0, "TrapClockHandler: removed process (%d) from delay queue and added to ready queue.\n", toRemove->pid);

                // Now reset the temp pointer.
                temp = temp2;
            } else {
                temp = temp->next;
            }
        }
    }


    /* Check if the current process has exhausted its time period. */
    if (curr_proc->runningTime >= 2) { 
        // Reset the running time for the current process.
        curr_proc->runningTime = 0;

        // Perform a context switch to the next process in the round-robin manner.
        // Schedule next process to run 
        if (!(IsLinkedListEmpty(runningQueue))) {
            PCB *pcb_2 = dequeueFromList(runningQueue);

            // Do not enqueue idle process to runningQueue
            if (curr_proc != idle_pcb){
                enqueueToList(runningQueue, curr_proc);
            }
            
            ContextSwitch(MySwitchFunc, curr_proc->ctx, curr_proc, pcb_2);
        } 
        // If no ready process, continue executing the current process for two more clock tick
    }

    (void) info; // Prevent compilation errors.
}

/* 
 * Handles illegal operations by terminating the offending process
 * and reporting the specific cause of the illegal operation.
 */
void TrapIllegalHandler(ExceptionInfo *info){
    int curr_pid = curr_proc->pid;
    TracePrintf(0, "TrapIllegalHandler: entered by process (%d).\n", curr_proc->pid);

    // Determine the reason for the illegal operation based on the 'code' field.
    char *reason;
    switch (info->code) {
        case TRAP_ILLEGAL_ILLOPC:
            reason = "Illegal opcode";
            break;
        case TRAP_ILLEGAL_ILLOPN:
            reason = "Illegal operand";
            break;
        case TRAP_ILLEGAL_ILLADR:
            reason = "Illegal addressing mode";
            break;
        case TRAP_ILLEGAL_ILLTRP:
            reason = "Illegal software trap";
            break;
        case TRAP_ILLEGAL_PRVOPC:
            reason = "Privileged opcode";
            break;
        case TRAP_ILLEGAL_PRVREG:
            reason = "Privileged register";
            break;
        case TRAP_ILLEGAL_COPROC:
            reason = "Coprocessor error";
            break;
        case TRAP_ILLEGAL_BADSTK:
            reason = "Bad stack";
            break;
        case TRAP_ILLEGAL_KERNELI:
            reason = "Linux kernel sent SIGILL";
            break;
        case TRAP_ILLEGAL_USERIB:
            reason = "Received SIGILL or SIGBUS from user";
            break;
        case TRAP_ILLEGAL_ADRALN:
            reason = "Invalid address alignment";
            break;
        case TRAP_ILLEGAL_ADRERR:
            reason = "Non-existent physical address";
            break;
        case TRAP_ILLEGAL_OBJERR:
            reason = "Object-specific HW error";
            break;
        case TRAP_ILLEGAL_KERNELB:
            reason = "Linux kernel sent SIGBUS";
            break;
        default:
            reason = "Unknown reason";
            break;
    }
    // Print the error message to stderr
    fprintf(stderr, "Illegal operation detected in process %d: %s\n", curr_pid, reason);

    // Now, terminate process.
    TerminateProcess(curr_proc, ERROR);
}

/* 
 * Helper function to terminate the process and perform ContextSwitch 
 */
void TerminateProcess(PCB *pcb, int exit_status){
    TracePrintf(0, "TerminateProcess: entered for process id (%d)\n", pcb->pid);
    // Handle error if PCB is not found
    if (pcb == NULL){
        perror("Inputted pcb is null at TerminateProcess()");
    }
    
    // If this process has a parent
    if (pcb->parent_pid != -1)
        // Update parent's exited_children and runnning_children queue
        notifyParent(pcb->parent_pid, pcb, exit_status);
    
    // Notify all children that they are now orphan
    notifyChildren(pcb);

    // Set the flag indicating that we should delete this process when doing ContextSwitch
    pcb->isTerminated = 1;

    // Perform a context switch to the next process (and also terminate current running process)
    scheduleNextProcess();
}

/* 
 * Helper function to update parent's pcb about child's exit_status 
 */
void notifyParent(int parent_pid, PCB *child_pcb, int exit_status){

    // Get the PCB for parent process
    PCB* parent_pcb = child_pcb->parent;

    // If the exiting process does not have a parent (i.e. idle process or terminated parent), we can skip these steps.
    if (parent_pcb != NULL) {
        // Remove this exited child from parent's running_children list
        if (SearchAndRemovePCB(parent_pcb->running_children, child_pcb->pid) == ERROR){
            perror("Error in removing child's PCB from parent's running_children list");
        }

        // If parent was waiting to collect a child, we remove it from wait queue and add it to ready queue.
        if (SearchAndRemovePCB(wait_queue, parent_pid) == 1){
            // If parent found in wait queue, now add it to ready queue.
            enqueueToList(runningQueue, parent_pcb);
        }

        // Push children exit status to exited_children list for parent's reference
        // It is because after freeing memory for child process, this child_pcb will be freed as well
        exit_child_status *exited_child_node = malloc(sizeof(exit_child_status));
        exited_child_node->status = exit_status;
        exited_child_node->pid = child_pcb->pid;
        enqueueToList(parent_pcb->exited_children, exited_child_node);
    }
}

/* 
 * Helper function to notify all children that they are now orphan 
 */
void notifyChildren(PCB *pcb){

    // The PCB is null, nothing to update.
    if (pcb == NULL) {
        perror("pcb is null at notifyChildren"); 
    }

    ListNode* current = pcb->running_children->head; // Start at the head of the children list

    while (current != NULL) {
        PCB* child_pcb = (PCB*)current->data; // Cast the data to a PCB pointer
        if (child_pcb != NULL) {
            child_pcb->parent_pid = -1; // Indicate that the child is orphan
            child_pcb->parent = NULL;
        }
        current = current->next; // Move to the next child in the list
    }
}


/* 
 * Helper function to schedule next runnable process 
 */
void scheduleNextProcess(){
    // Schedule next process to run 
    if (!(IsLinkedListEmpty(runningQueue))) {
        PCB *pcb_2 = dequeueFromList(runningQueue);
        ContextSwitch(MySwitchFunc, curr_proc->ctx, curr_proc, pcb_2);
    } else {
        TracePrintf(0, "scheduleNextProcess: switching from  (%d) to idle.\n", curr_proc->pid);
        // If no ready process, switch to idle process
        ContextSwitch(MySwitchFunc, curr_proc->ctx, curr_proc, idle_pcb);
    }
}

/* Handles memory access violations.*/
void TrapMemoryHandler(ExceptionInfo *info){
    TracePrintf(0, "TrapMemoryHandlerq: entered by process (%d).\n", curr_proc->pid);


    // Handle error:
    if (info == NULL) {
        perror("ExceptionInfo is NULL at TrapMemoryHandler()");
        return;
    }

    // Calculate the page index of the faulting address.
    unsigned int faultingPageIndex = DOWN_TO_PAGE((unsigned long)info->addr) >> PAGESHIFT;

    TracePrintf(0, "TrapMemoryHandler: faultingPageIndex (%d)\n", faultingPageIndex);

    // Calculate the number of page demanded
    unsigned int num_page_demanded = curr_proc->uStack_bottom - faultingPageIndex;
    
    TracePrintf(0, "TrapMemoryHandler: num_page_demanded (%d)\n", num_page_demanded);
    /* Check if 
            1. faulting address is below uStack bottom and above uheap top,  
               + 1 make sure that we leave one free page between uheap and uStack
            2. make sure we have enough physcial memory to allocate 
    */
    if (faultingPageIndex > (UP_TO_PAGE(curr_proc->brk) >> PAGESHIFT) + 1 
        && faultingPageIndex < curr_proc->uStack_bottom
        && num_page_demanded <= (unsigned long) free_pframe_count){

        unsigned int i = 0;
        unsigned int ptr_bottom = curr_proc->uStack_bottom;

        while (i < num_page_demanded){
            // Move stack bottom up one page.
            ptr_bottom--;

            if (curr_proc->pgt_r0[ptr_bottom].valid == 0) {
                // Allocate a new page to extend the stack.
                long newFrame;
                if ((newFrame = AllocateFreePage()) == -1) {
                    perror("No more free Physical memory at TrapMemoryHandler()");
                    return;
                }
                // Update the page table entry for the new stack page.
                curr_proc->pgt_r0[ptr_bottom].valid = 1;
                curr_proc->pgt_r0[ptr_bottom].pfn = (unsigned int)newFrame;
                curr_proc->pgt_r0[ptr_bottom].uprot = PROT_READ | PROT_WRITE; 
                curr_proc->pgt_r0[ptr_bottom].kprot = PROT_READ | PROT_WRITE;

                num_page_demanded--;
            } else {
                perror("Trying to allocate free PTE but it is mapped at TrapMemoryHandler()");
                return;
            }   
        }
        // Set location of new user stack bottom.
        curr_proc->uStack_bottom = faultingPageIndex;
    } else {
        // The faulting address is not within the stack's auto-extension range.
        // This is an illegal memory access, so terminate the process.

        // printing error message 
        fprintf(stderr, "Error: Process %d tried to access an illegal address at 0x%lx; terminating process.\n", 
        curr_proc->pid, (unsigned long)info->addr);

        TerminateProcess(curr_proc, ERROR);
        return;
    }
}
/* Handle arithmetic errors. */
void TrapMathHandler(ExceptionInfo *info){
    TracePrintf(0, "TrapMathHandler: entered by process (%d).\n", curr_proc->pid);

    // Handle error:
    if (info == NULL) {
        perror("ExceptionInfo is NULL at TrapMathHandler()");
        return;
    }

    // printing error message 
    fprintf(stderr, "Arithmetic error triggered by Process %d\n", curr_proc->pid);

    // Terminate the process
    TerminateProcess(curr_proc, ERROR);

}

/* Processes terminal input by receiving text for a specific terminal. */
void TrapReceiveHandler(ExceptionInfo *info){
    TracePrintf(0, "TrapReceiveHandler: entered by process (%d).\n", curr_proc->pid);

    if (info == NULL || info->code < 0 || info->code >= NUM_TERMINALS) {
        fprintf(stderr, "Error at TrapReceiveHandler()\n");
        return;
    }

    // Retrieve Terminal ID
    int tty_id = info->code; 
    TracePrintf(0, "Retrieve Terminal ID: (%d).\n", tty_id);

    // Allocate a new textStruct for the line of text
    textStruct* newText = malloc(sizeof(textStruct));  

    // Initialize ptr to 0
    newText->ptr = 0;

    // Perform the receive operation for the terminal
    newText->length = TtyReceive(tty_id, newText->line, TERMINAL_MAX_LINE);

    enqueueToList(inputBuffer[tty_id], newText);

    // Set the flag indicating that terminal tty_id is ready to read
    readReady[tty_id] = 1;

    // while there are some processes waiting on TtyRead, and there
    // are available text to read, unblock it to read the Terminal
    int i = 0;
    while (!IsLinkedListEmpty(readQueue[tty_id]) && readReady[tty_id] == 1){
        PCB *pcb2;
        if ((pcb2 = dequeueFromList(readQueue[tty_id])) == NULL){
            fprintf(stderr, "readQueue should not return null at TrapReceiveHandler()\n");
            return;
        }
        enqueueToList(runningQueue, curr_proc);
        ContextSwitch(MySwitchFunc, curr_proc->ctx, curr_proc, pcb2);

        // Below should be redundant since we set this when at TtyRead
        if (IsLinkedListEmpty(inputBuffer[tty_id])){
            // All text has been read, reset the read-ready flag
            readReady[tty_id] = -1;
        }
        TracePrintf(0, "Iteration: (%d).\n", i++);
    }

    return;
}

/* Manages terminal output completion. */
void TrapTransmitHandler(ExceptionInfo *info){
    TracePrintf(0, "TrapTransmitHandler: entered by process (%d).\n", curr_proc->pid);
    
    if (info == NULL || info->code < 0 || info->code >= NUM_TERMINALS) {
        fprintf(stderr, "Error at TrapTransmitHandler()\n");
        return;
    }

    // Retrieve the terminal ID from the ExceptionInfo
    int tty_id = info->code;

    // Find the PCB that called the TtyTransmit, which caused this interrupt.
    PCB *pcb2;
    pcb2 = transmitPCB[tty_id];
    
    // Since TtyTransmit successfully done, we set that write is again possible.
    writeReady[tty_id] = 1;

    // Switch to the process that initiate this TtyWrite
    enqueueToList(runningQueue, curr_proc);
    ContextSwitch(MySwitchFunc, curr_proc->ctx, curr_proc, pcb2);

    // If there are more processes waiting to execute TtyWrite, 
    // unblock one by calling TtyTransmit - if write is ready.
    if (!IsLinkedListEmpty(writeQueue[tty_id]) && writeReady[tty_id] == 1) {

        // Otherwise, we can ttytransmit now.
        PCB *blocked_pcb;
        if ((blocked_pcb = peekFromList(writeQueue[tty_id])) == NULL) {
            fprintf(stderr, "writeQueue should not be empty given that IsLinkedListEmpty returns false at TrapTransmitHandler()\n");
            return;
        }

        TracePrintf(0, "TrapTransmitHandler: switching back to write for process (%d)\n", blocked_pcb->pid);
        
        // Dequeue the blocked process and add it to ready/running queue.
        dequeueFromList(writeQueue[tty_id]);
        writeReady[tty_id] = -1; 
        transmitPCB[tty_id] = blocked_pcb;
        TtyTransmit(tty_id, blocked_pcb->writeRequest, blocked_pcb->writeLength);
    }

    return;
}