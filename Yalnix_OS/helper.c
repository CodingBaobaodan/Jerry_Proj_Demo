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
 *  Load a program into the current process's address space.  The
 *  program comes from the Unix file identified by "name", and its
 *  arguments come from the array at "args", which is in standard
 *  argv format.
 *
 *  Returns:
 *      0 on success
 *     -1 on any error for which the current process is still runnable
 *     -2 on any error for which the current process is no longer runnable
 *
 *  This function, after a series of initial checks, deletes the
 *  contents of Region 0, thus making the current process no longer
 *  runnable.  Before this point, it is possible to return ERROR
 *  to an Exec() call that has called LoadProgram, and this function
 *  returns -1 for errors up to this point.  After this point, the
 *  contents of Region 0 no longer exist, so the calling user process
 *  is no longer runnable, and this function returns -2 for errors
 *  in this case.
 */
int
LoadProgram(char *name, char **args, ExceptionInfo *info)
{
    int fd;
    int status;
    struct loadinfo li;
    char *cp;
    char *cp2;
    char **cpp;
    char *argbuf;
    int i;
    unsigned long argcount;
    int size;
    int text_npg;
    int data_bss_npg;
    int stack_npg;

    TracePrintf(0, "LoadProgram '%s', args %p\n", name, args);

    if ((fd = open(name, O_RDONLY)) < 0) {
	TracePrintf(0, "LoadProgram: can't open file '%s'\n", name);
	return (-1);
    }

    status = LoadInfo(fd, &li);
    TracePrintf(0, "LoadProgram: LoadInfo status %d\n", status);
    switch (status) {
	case LI_SUCCESS:
	    break;
	case LI_FORMAT_ERROR:
	    TracePrintf(0,
		"LoadProgram: '%s' not in Yalnix format\n", name);
	    close(fd);
	    return (-1);
	case LI_OTHER_ERROR:
	    TracePrintf(0, "LoadProgram: '%s' other error\n", name);
	    close(fd);
	    return (-1);
	default:
	    TracePrintf(0, "LoadProgram: '%s' unknown error\n", name);
	    close(fd);
	    return (-1);
    }
    TracePrintf(0, "text_size 0x%lx, data_size 0x%lx, bss_size 0x%lx\n",
	li.text_size, li.data_size, li.bss_size);
    TracePrintf(0, "entry 0x%lx\n", li.entry);

    /*
     *  Figure out how many bytes are needed to hold the arguments on
     *  the new stack that we are building.  Also count the number of
     *  arguments, to become the argc that the new "main" gets called with.
     */
    size = 0;
    for (i = 0; args[i] != NULL; i++) {
	size += strlen(args[i]) + 1;
    }
    argcount = i;
    TracePrintf(0, "LoadProgram: size %d, argcount %d\n", size, argcount);

    /*
     *  Now save the arguments in a separate buffer in Region 1, since
     *  we are about to delete all of Region 0.
     */
    cp = argbuf = (char *)malloc(size);
    for (i = 0; args[i] != NULL; i++) {
	strcpy(cp, args[i]);
	cp += strlen(cp) + 1;
    }

    TracePrintf(0, "LoadProgram: variable argbuf address is '%lx'\n", argbuf);
  
    /*
     *  The arguments will get copied starting at "cp" as set below,
     *  and the argv pointers to the arguments (and the argc value)
     *  will get built starting at "cpp" as set below.  The value for
     *  "cpp" is computed by subtracting off space for the number of
     *  arguments plus 4 (for the argc value, a 0 (AT_NULL) to
     *  terminate the auxiliary vector, a NULL pointer terminating
     *  the argv pointers, and a NULL pointer terminating the envp
     *  pointers) times the size of each (sizeof(void *)).  The
     *  value must also be aligned down to a multiple of 8 boundary.
     */
    cp = ((char *)USER_STACK_LIMIT) - size;
    cpp = (char **)((unsigned long)cp & (-1 << 4));	/* align cpp */
    cpp = (char **)((unsigned long)cpp - ((argcount + 4) * sizeof(void *)));

    TracePrintf(0, "LoadProgram: variable cp address is '%lx'\n", cp);
    TracePrintf(0, "LoadProgram: variable cpp address is '%lx'\n", cpp);

    text_npg = li.text_size >> PAGESHIFT;
    data_bss_npg = UP_TO_PAGE(li.data_size + li.bss_size) >> PAGESHIFT;
    stack_npg = (USER_STACK_LIMIT - DOWN_TO_PAGE(cpp)) >> PAGESHIFT;

    TracePrintf(0, "LoadProgram: text_npg %d, data_bss_npg %d, stack_npg %d\n",
	text_npg, data_bss_npg, stack_npg);

    /*
     *  Make sure we have enough *virtual* memory to fit everything within
     *  the size of a page table, including leaving at least one page
     *  between the heap and the user stack
     */
    if (MEM_INVALID_PAGES + text_npg + data_bss_npg + 1 + stack_npg +
	1 + KERNEL_STACK_PAGES > PAGE_TABLE_LEN) {
	TracePrintf(0,
	    "LoadProgram: program '%s' size too large for VIRTUAL memory\n",
	    name);
	free(argbuf);
	close(fd);
	return (-1);
    }

    /*
     *  And make sure there will be enough *physical* memory to
     *  load the new program.
     */
    // >>>> The new program will require text_npg pages of text,
    // >>>> data_bss_npg pages of data/bss, and stack_npg pages of
    // >>>> stack.  In checking that there is enough free physical
    // >>>> memory for this, be sure to allow for the physical memory
    // >>>> pages already allocated to this process that will be
    // >>>> freed below before we allocate the needed pages for
    // >>>> the new program being loaded.

    int alr_alloc_pages = 0;
    for (i = 0; i < PAGE_TABLE_LEN - KERNEL_STACK_PAGES; i++) {
        if (curr_proc->pgt_r0[i].valid == 1) {
            alr_alloc_pages++;
        }
    }

    if (text_npg + data_bss_npg + stack_npg > free_pframe_count + alr_alloc_pages) {
	TracePrintf(0,
	    "LoadProgram: program '%s' size too large for PHYSICAL memory\n",
	    name);
	free(argbuf);
	close(fd);
	return (-1);
    }

    // >>>> Initialize sp for the current process to (void *)cpp.
    // >>>> The value of cpp was initialized above.
    info->sp = (void *)cpp;
    

    /*
     *  Free all the old physical memory belonging to this process,
     *  but be sure to leave the kernel stack for this process (which
     *  is also in Region 0) alone.
     */
    // >>>> Loop over all PTEs for the current process's Region 0,
    // >>>> except for those corresponding to the kernel stack (between
    // >>>> address KERNEL_STACK_BASE and KERNEL_STACK_LIMIT).  For
    // >>>> any of these PTEs that are valid, free the physical memory
    // >>>> memory page indicated by that PTE's pfn field.  Set all
    // >>>> of these PTEs to be no longer valid.

    for (i = 0; i < PAGE_TABLE_LEN - KERNEL_STACK_PAGES; i++) {
         if (curr_proc->pgt_r0[i].valid == 1) {
            // Free page with associated pfn.
            FreePhysicalPage(curr_proc->pgt_r0[i].pfn);

            // Set virtual page as no longer valid.
            curr_proc->pgt_r0[i].valid = 0;
        }
    }


    /*
     *  Fill in the page table with the right number of text,
     *  data+bss, and stack pages.  We set all the text pages
     *  here to be read/write, just like the data+bss and
     *  stack pages, so that we can read the text into them
     *  from the file.  We then change them read/execute.
     */

    // >>>> Leave the first MEM_INVALID_PAGES number of PTEs in the
    // >>>> Region 0 page table unused (and thus invalid)

    for (i = 0; i < MEM_INVALID_PAGES; i++) {
        curr_proc->pgt_r0[i].valid = 0;
    }

    /* First, the text pages */
    // >>>> For the next text_npg number of PTEs in the Region 0
    // >>>> page table, initialize each PTE:
    // >>>>     valid = 1
    // >>>>     kprot = PROT_READ | PROT_WRITE
    // >>>>     uprot = PROT_READ | PROT_EXEC
    // >>>>     pfn   = a new page of physical memory


    for (i = MEM_INVALID_PAGES; i < MEM_INVALID_PAGES + text_npg; i++) {
        curr_proc->pgt_r0[i].valid = 1;
        curr_proc->pgt_r0[i].kprot = PROT_READ | PROT_WRITE;
        curr_proc->pgt_r0[i].uprot = PROT_READ | PROT_EXEC;

        long frame_num;
        if ((frame_num = AllocateFreePage()) < 0) {
            TracePrintf(0, "LoadProgram: no more physical pages left for program '%s'\n", name);
	        free(argbuf);
	        close(fd);
	        return (-1);
        } else {
            curr_proc->pgt_r0[i].pfn = (unsigned int) frame_num;
        }
    }

    /* Then the data and bss pages */
    // >>>> For the next data_bss_npg number of PTEs in the Region 0
    // >>>> page table, initialize each PTE:
    // >>>>     valid = 1
    // >>>>     kprot = PROT_READ | PROT_WRITE
    // >>>>     uprot = PROT_READ | PROT_WRITE
    // >>>>     pfn   = a new page of physical memory


    for (i = MEM_INVALID_PAGES + text_npg; i < MEM_INVALID_PAGES + text_npg + data_bss_npg; i++) {
        curr_proc->pgt_r0[i].valid = 1;
        curr_proc->pgt_r0[i].kprot = PROT_READ | PROT_WRITE;
        curr_proc->pgt_r0[i].uprot = PROT_READ | PROT_WRITE;

        long frame_num;
        if ((frame_num = AllocateFreePage()) < 0) {
            TracePrintf(0, "LoadProgram: no more physical pages left for program '%s'\n", name);
	        free(argbuf);
	        close(fd);
	        return (-1);
        } else {
            curr_proc->pgt_r0[i].pfn = (unsigned int) frame_num;
        }
    }

    curr_proc->brk = (MEM_INVALID_PAGES + text_npg + data_bss_npg) << PAGESHIFT; // Sets new brk.


    /* And finally the user stack pages */
    // >>>> For stack_npg number of PTEs in the Region 0 page table
    // >>>> corresponding to the user stack (the last page of the
    // >>>> user stack *ends* at virtual address USER_STACK_LIMIT),
    // >>>> initialize each PTE:
    // >>>>     valid = 1
    // >>>>     kprot = PROT_READ | PROT_WRITE
    // >>>>     uprot = PROT_READ | PROT_WRITE
    // >>>>     pfn   = a new page of physical memory

    int ustack_pg_limit = USER_STACK_LIMIT >> PAGESHIFT;
    for (i = ustack_pg_limit - stack_npg; i < ustack_pg_limit ; i++) {
        curr_proc->pgt_r0[i].valid = 1;
        curr_proc->pgt_r0[i].kprot = PROT_READ | PROT_WRITE;
        curr_proc->pgt_r0[i].uprot = PROT_READ | PROT_WRITE;

        long frame_num;
        if ((frame_num = AllocateFreePage()) < 0) {
            TracePrintf(0, "LoadProgram: no more physical pages left for program '%s'\n", name);
	        free(argbuf);
	        close(fd);
	        return (-1);
        } else {
            curr_proc->pgt_r0[i].pfn = (unsigned int) frame_num;
            TracePrintf(0, "LoadProgram: user stack idx filled was '%d'\n", i);
        }
    }

    curr_proc->uStack_bottom = ustack_pg_limit - stack_npg;

    /*
     *  All pages for the new address space are now in place.  Flush
     *  the TLB to get rid of all the old PTEs from this process, so
     *  we'll be able to do the read() into the new pages below.
     */
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);

    /*
     *  Read the text and data from the file into memory.
     */
    
    if (read(fd, (void *)MEM_INVALID_SIZE, li.text_size+li.data_size) != (long) (li.text_size+li.data_size)) {
	TracePrintf(0, "LoadProgram: couldn't read for '%s'\n", name);
	free(argbuf);
	close(fd);
	// >>>> Since we are returning -2 here, this should mean to
	// >>>> the rest of the kernel that the current process should
	// >>>> be terminated with an exit status of ERROR reported
	// >>>> to its parent process.
	return (-2);
    }

    close(fd);			/* we've read it all now */

    /*
     *  Now set the page table entries for the program text to be readable
     *  and executable, but not writable.
     */
    // >>>> For text_npg number of PTEs corresponding to the user text
    // >>>> pages, set each PTE's kprot to PROT_READ | PROT_EXEC.

    for (i = MEM_INVALID_PAGES; i < MEM_INVALID_PAGES + text_npg; i++) {
        curr_proc->pgt_r0[i].kprot = PROT_READ | PROT_EXEC;
    }

    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);

    /*
     *  Zero out the bss
     */
    memset((void *)(MEM_INVALID_SIZE + li.text_size + li.data_size),
	'\0', li.bss_size);

    /*
     *  Set the entry point in the ExceptionInfo.
     */
    // >>>> Initialize pc for the current process to (void *)li.entry
    info->pc = (void *)li.entry;

    /*
     *  Now, finally, build the argument list on the new stack.
     */
    *cpp++ = (char *)argcount;		/* the first value at cpp is argc */
    cp2 = argbuf;
    for (i = 0; (unsigned long) i < argcount; i++) {      /* copy each argument and set argv */
    TracePrintf(0, "LoadProgram: iterator i is at vlaue '%d'\n", i);
	*cpp++ = cp;
	strcpy(cp, cp2);
	cp += strlen(cp) + 1;
	cp2 += strlen(cp2) + 1;
    }
    free(argbuf);
    *cpp++ = NULL;	/* the last argv is a NULL pointer */
    *cpp++ = NULL;	/* a NULL pointer for an empty envp */
    *cpp++ = 0;		/* and terminate the auxiliary vector */

    /*
     *  Initialize all regs[] registers for the current process to 0,
     *  initialize the PSR for the current process also to 0.  This
     *  value for the PSR will make the process run in user mode,
     *  since this PSR value of 0 does not have the PSR_MODE bit set.
     */
    // >>>> Initialize regs[0] through regs[NUM_REGS-1] for the
    // >>>> current process to 0.
    // >>>> Initialize psr for the current process to 0.

    for (i = 0; i < NUM_REGS; i++) {
        info->regs[i] = 0;
    }
    info->psr = 0;

    TracePrintf(0, "LoadProgram: complete\n");

    return (0);
}

/*******   HELPER FUNCTION FOR CONTEXT SWITCHING. *******/

/*******   HELPER FUNCTIONS FOR PHYSICAL PAGES AND PCB DATA STRUCTURES. *******/

/* 
 * Helper function to deallocate physical page with specified
 * pfn, and set it as a free page. Inserts it to front of the 
 * linked list of free pages, and increments count.
 */
void
FreePhysicalPage(unsigned int pfn)
{   
    TracePrintf(0, "FreePhysicalPage: freeing pfn (%d)\n", pfn);

    // Create new frame.
    pframe *temp = (pframe *) malloc(sizeof(pframe));
    
    // If we cannot free a physical page, we need to Halt process as it does not have enough memory to even free pages.
    if (temp == NULL) {
        printf("No memory left to free physical page. Now Halting kernel.\n");
        Halt();
    }

    temp->frame_num = pfn;

    // Insert into linked list of free frames.
    temp->next = free_pframe_head;
    free_pframe_head = temp;

    // Increment free page counter.
    free_pframe_count++;
}


/* 
 * Helper function to allocate free physical page. Updates
 * free frame linked list and counter. Frees memory of now used
 * frame. Returns the frame number of the allocated frame.
 */
long
AllocateFreePage()
{
    // Check if any free pages left.
    if (free_pframe_head == NULL) {
        return (-1); // Error code.
    }

    // If there is, take the head as new frame.
    pframe *temp = free_pframe_head;
    pframe *next = free_pframe_head->next;
    unsigned int free_frame_num = free_pframe_head->frame_num;

    // Free frame.
    free(temp);

    // Decrement counter, and change head.
    free_pframe_count--;
    free_pframe_head = next;
    
    TracePrintf(0, "AllocateFreePage: allocating pfn (%d)\n", free_frame_num);

    return free_frame_num;
}


/* 
 * Helper function to create PCB including all
 * allocation of memory and setting of fields. Returns
 * pointer to PCB that was just created. 
 */
struct PCB*
CreatePCB(PCB* parent)
{
    TracePrintf(0, "CreatePCB: creating PCB for new process.\n");

    // Build PCB structure.
    PCB *new_pcb = malloc(sizeof(PCB)); 

    // Fields.
    if (parent == NULL) {
        new_pcb->parent_pid = 0;
        new_pcb->parent = NULL;
    } else {
        new_pcb->parent_pid = parent->pid;
        new_pcb->parent = parent;
    }
    new_pcb->pid = pid_counter++;
    new_pcb->runningTime = 0;
    new_pcb->needs_copy = -1;
    new_pcb->isDelayed = -1;
    new_pcb->isTerminated = -1;
    new_pcb->exited_children = CreateLinkedList();
    new_pcb->running_children = CreateLinkedList();

    // Allocate memory for page table, region 0.
    if (AllocateRegion0PageTable(new_pcb) == -1) {
        // If we run into error allocating page table, return NULL pcb.
        return NULL;
    }

    // More fields for pointers to stack.
    new_pcb->uStack_bottom = USER_STACK_LIMIT;
    new_pcb->brk = MEM_INVALID_SIZE;

    // Set invalid pte entries in page table as invalid (as allocated pfn may have been previously used).
    int i = 0;
    for (; i < MEM_INVALID_PAGES; i++) {
        new_pcb->pgt_r0[i].valid = 0;
    }
    
    // Allocate memory for saved context.
    new_pcb->ctx = (SavedContext *) malloc(sizeof(SavedContext));

    // Check if any fields in PCB are NULL; if so, we need to return NULL to signal to we could not create a PCB struct.
    if (new_pcb == NULL || new_pcb->exited_children == NULL || new_pcb->running_children == NULL || new_pcb->ctx == NULL) {
        return NULL;
    }

    TracePrintf(0, "CreatePCB: finished PCB for new process.\n");

    return new_pcb;
}

/* 
 * Helper function to create PCB struct for idle process. Must be
 * passed an already-allocated page table region 0.
 */
struct PCB*
CreateIdlePCB()
{
    
    TracePrintf(0, "CreateIdlePCB: creating PCB for idle process.\n");

    // Build PCB structure.
    PCB *new_pcb = (PCB *) malloc(sizeof(PCB)); 

    // Fields.
    new_pcb->parent_pid = -1;
    new_pcb->parent = NULL;
    new_pcb->pid = pid_counter++;
    new_pcb->runningTime = 0;
    new_pcb->needs_copy = -1;
    new_pcb->isDelayed = -1;
    new_pcb->isTerminated = -1;
    new_pcb->exited_children = CreateLinkedList();
    new_pcb->running_children = CreateLinkedList();

    // Set pointer to region 0 page table for init process.
    new_pcb->pgt_r0 = pgt_r0;
    new_pcb->brk = MEM_INVALID_SIZE;

    // No user stack yet.
    new_pcb->uStack_bottom = USER_STACK_LIMIT;
    new_pcb->pgt_r0_paddr = (unsigned long) new_pcb->pgt_r0;
    
    // Allocate memory for saved context.
    new_pcb->ctx = (SavedContext *) malloc(sizeof(SavedContext));
    
    // Check if any fields in PCB are NULL; if so, we need to return NULL to signal to we could not create a PCB struct.
    if (new_pcb == NULL || new_pcb->exited_children == NULL || new_pcb->running_children == NULL || new_pcb->ctx == NULL) {
        return NULL;
    }

    TracePrintf(0, "CreateIdlePCB: finished PCB for idle process.\n");

    return new_pcb;
}

/* 
 * Helper function to allocate a region 0 page table for a new process.
 * Saves space by allocating two page tables per page in memory.
 */
int
AllocateRegion0PageTable(PCB* pcb)
{   
    TracePrintf(0, "AllocateRegion0PageTable: trying to allocate page table.\n");

    // If the page for the page table region 0 is already half used.
    if (is_half_used == 1) {
        TracePrintf(0, "AllocateRegion0PageTable: already has half used page.\n");
        // Set the pcb's pgt_r0 to second half of already used page
        pcb->pgt_r0 = (struct pte*) (addr_next_pgt_r0 + PAGESIZE/2);

        // Set the pcb's paddr for its page table
        pcb->pgt_r0_paddr = curr_pgt_paddr + PAGESIZE/2;

        TracePrintf(0, "AllocateRegion0PageTable: physical addr is (0x%lx).\n", pcb->pgt_r0_paddr);

        // Reset half-used flag.
        is_half_used = 0;

        // Decrement addr for next page talbe region 0 memory location
        addr_next_pgt_r0 -= PAGESIZE;

        return (1); // Success
    }
    // If we need to allocate a new page.
    else {
        TracePrintf(0, "AllocateRegion0PageTable: allocating new page.\n");
        // Set the pcb's pgt_r0 to first half of new page
        pcb->pgt_r0 = (struct pte*) (addr_next_pgt_r0);

        TracePrintf(0, "AllocateRegion0PageTable: address of new pt r0 is %lx.\n", pcb->pgt_r0);

        // Now, we need to get a free frame and update region 1's page table as needed
        unsigned long r1_idx = ((unsigned long) addr_next_pgt_r0 - VMEM_1_BASE) >> PAGESHIFT;

        // If we run into already allocated memory, we need to exit process.
        if (pgt_r1[r1_idx].valid == 1) {
            return (-1); // Error
        }

        // Otherwise, get a new page and set it.
        long new_page_pfn = AllocateFreePage();
        curr_pgt_paddr = new_page_pfn << PAGESHIFT;
        pcb->pgt_r0_paddr = curr_pgt_paddr;


        TracePrintf(0, "AllocateRegion0PageTable: physical addr is (0x%lx) and pfn (%d).\n", pcb->pgt_r0_paddr, new_page_pfn);

        // Check if we have memory or not.
        if (new_page_pfn == -1) {
            return (-1); // Error
        }

        pgt_r1[r1_idx].valid = 1;
        pgt_r1[r1_idx].pfn = (unsigned int) new_page_pfn;
        pgt_r1[r1_idx].uprot = PROT_NONE;
        pgt_r1[r1_idx].kprot = (PROT_READ | PROT_WRITE);

        WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1);

        // Reset half-used flag.
        is_half_used = 1;

        return (1); // Success
    }
    
    (void) pcb;
}
