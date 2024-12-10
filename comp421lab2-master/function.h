#ifndef _function_h
#define _function_h

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include </clear/courses/comp421/pub/include/comp421/yalnix.h>
#include </clear/courses/comp421/pub/include/comp421/hardware.h>
#include </clear/courses/comp421/pub/include/comp421/loadinfo.h>


// Forward declaration of PCB and exit_child_status structure (make the complier happy)
typedef struct PCB PCB;
typedef struct exit_child_status exit_child_status;

/* *************************** Linked List *************************** */
typedef struct ListNode {
    void* data;                 // Generic data pointer
    struct ListNode* next;      // Pointer to the next node
    struct ListNode* previous; // Pointer to the previous node in the list
} ListNode;

typedef struct LinkedList {
    ListNode* head;            // Pointer to the first node in the list
    ListNode* tail;            // Pointer to the first node in the list
} LinkedList;

// Function prototypes
extern LinkedList* CreateLinkedList();
extern void enqueueToList(LinkedList* list, void* data);
extern void* dequeueFromList(LinkedList* list);
extern void freeListContents(LinkedList* list);
extern void freeListContentsExitChildren(LinkedList* list);
extern void* peekFromList(LinkedList* list);
extern int SearchAndRemovePCB(LinkedList* list, int pid);
extern int IsLinkedListEmpty(LinkedList *list);
extern PCB* SearchAndReturnPCB(LinkedList* list, int pid);
extern void printLinkedList(LinkedList* list);
/* *************************** Linked List *************************** */

// Physical frame struct.
typedef struct pframe {
    unsigned long frame_num; // Frame number of the physical frame
    struct pframe* next; // Pointer to next free physical frame
} pframe;

typedef struct textStruct {
    char line[TERMINAL_MAX_LINE];
    int length; // Record the lenght of line that has not been read
    int ptr; // Record the index in the line where it has not been read
} textStruct;

/* *************************** Define PCB *************************** */
struct PCB {
    int pid; // Process's ID
    int parent_pid; // Process's parent PID, -1 means an orphan process
    struct PCB* parent; // PCB of parent. NULL if no parent.
    unsigned int runningTime; // Record the total # of clock ticks undergone by this process after most recent context switch
    unsigned int delay_until; // Records (if a process is delayed) what time it should delay until (compare with runningTime)
    int needs_copy; // Flag that indicates whether this process (p1) should be copied to another (p2), 1 if Yes, -1 if No
    int isDelayed; // Flag that indicates whether this process is delayed. 1 if Yes, -1 if No.
    int isTerminated; // Flag that indicate whether this process should be terminated, 1 means Yes, -1 means No (do not terminate)

    LinkedList* exited_children; // FIFO for this process's children that exited but not yet collected by this process
    LinkedList* running_children; // FIFO to record this process's running children

    struct pte *pgt_r0; // Pointer to page table of region 0
    unsigned long pgt_r0_paddr; // Physical address to location of the next page table for region 0 (without offset)
    unsigned long brk; // Break of process heap - first memory address not part of heap.
    unsigned int uStack_bottom; // record the page table index for user stack that has range [uStack_bottom, uStack_top]

    char* writeRequest; // Points to buffer containing the bytes passed to TtyWrite call
    int writeLength; // Records length passed to a TtyWrite call

    SavedContext *ctx; // saved context of CPU state
};

struct exit_child_status {
    unsigned int pid; // Process ID of exited child
    int status; // Exit status of the exited child
};

/* *************************** Define PCB *************************** */

// Define the type of the function pointers
typedef void (*InterruptHandler)(ExceptionInfo *);

/* ######################## Global Variable ######################## */

extern unsigned int pid_counter;
extern PCB *curr_proc;  // Points to the current runnning process
extern int is_half_used; // Flag variable to indicate if region 1 page table page is half used

extern LinkedList* processQueue; // List of all processes 
extern LinkedList* runningQueue; // FIFO queue for all ready-running processes
extern LinkedList* delay_queue; // FIFO queue for all delayed processes
extern LinkedList* wait_queue; // FIFO queue for all waiting processes


extern InterruptHandler *interruptVectorTable; // Contains interrupt vectors
extern void *kernel_brk; // Initial kernel break location
extern int vm_enabled; // Flag to indicate if VM is enabled

// Page Tables
extern struct pte *pgt_r0;
extern struct pte *pgt_r1;
extern unsigned long addr_next_pgt_r0;
extern unsigned long curr_pgt_paddr;

// Idle process's PCB
extern PCB* idle_pcb;

// Total program running time.
extern unsigned long total_runningTime;

// Physical frame head, and count.
extern pframe *free_pframe_head;
extern int free_pframe_count;

// Terminal related Data Structure
extern LinkedList* inputBuffer[NUM_TERMINALS]; // Input buffer read for each terminal 
extern int readReady[NUM_TERMINALS]; // Flag to indicate if terminal i has text available to read, -1 means not ready. 1 means ready.
extern int writeReady[NUM_TERMINALS]; // Flag to indicate if terminal i is ready to be written, -1 means not ready. 1 means ready.
extern LinkedList* readQueue[NUM_TERMINALS]; // Queue that stores the process's PCB for a read request on terminal i
extern LinkedList* writeQueue[NUM_TERMINALS];// Queue that stores the process's PCB for a write request on terminal i
extern PCB* transmitPCB[NUM_TERMINALS]; // Array that stores the PCB's of processes that called a TtyTransmit in HandleTtyWrite, and is waiting for trap handler to context switch back to confirm it finished successfully.


/* ######################## Global Variable ######################## */

/* Loads in program */
extern int LoadProgram(char *name, char **args, ExceptionInfo *info);

/* Helper function for kernelStart */ 
extern void initKernel(void);
extern void initInterruptVector(void);
extern void InitMemoryManagement(unsigned int pmem_size);
extern void CreateIdleProcess(ExceptionInfo *info, char **cmd_args);
extern void CreateInitProcess(ExceptionInfo *info, char **cmd_args);

/* Helper function for ContextSwitch*/
extern SavedContext *MySwitchFunc(SavedContext *ctxp, void *p1, void* p2);

/* Helper function for Process related operation*/
extern void TerminateProcess(PCB *pcb, int exit_status);
extern void notifyParent(int parent_pid, PCB *child_pcb, int exit_status);
extern void notifyChildren(PCB *pcb);
extern void freeProcessResources(PCB *pcb);
extern void scheduleNextProcess();

/* Trap handler functions */ 
extern void TrapKernelHandler(ExceptionInfo *info);
extern void TrapClockHandler(ExceptionInfo *info);
extern void TrapIllegalHandler(ExceptionInfo *info);
extern void TrapMemoryHandler(ExceptionInfo *info);
extern void TrapMathHandler(ExceptionInfo *info);
extern void TrapReceiveHandler(ExceptionInfo *info);
extern void TrapTransmitHandler(ExceptionInfo *info);

/* Handler function for user's system call */ 
extern int HandleFork(ExceptionInfo *info);
extern int HandleExec(char *filename, char **argvec, ExceptionInfo *info);
extern void HandleExit(int status);
extern int HandleWait(int *status_ptr);
extern int HandleGetPid(void);
extern int HandleBrk(void *addr);
extern int HandleDelay(int clock_ticks);
extern int HandleTtyRead(int tty_id, void *buf, int len);
extern int HandleTtyWrite(int tty_id, void *buf, int len);

/* Helper functions for PCB creation.*/
extern struct PCB* CreatePCB(PCB* parent);
extern struct PCB* CreateIdlePCB();

/* Handler function for Page Table operation*/ 
extern void FreePhysicalPage(unsigned int pfn);
extern long AllocateFreePage();
extern int AllocateRegion0PageTable(PCB* pcb);


#endif // function_H