#include "function.h"
#include <stdlib.h> 

/*
 * Creates a LinkedList struct and returns it. On fail, we return NULL.
 */
LinkedList* CreateLinkedList() {
    LinkedList* list = (LinkedList*)malloc(sizeof(LinkedList));
    if (list != NULL) {
        list->head = NULL;
        list->tail = NULL;
    }
    return list;
}

/* 
 * Append the node to the tail of the list
 */
void enqueueToList(LinkedList* list, void* data) {
    ListNode* newNode = (ListNode*)malloc(sizeof(ListNode));
    if (newNode == NULL) {
        // Handle memory allocation failure if necessary
        return;
    }
    newNode->data = data;
    newNode->next = NULL;      // As it will be the last node
    newNode->previous = NULL;  // Will be set later if the list is not empty

    // If the list is empty, the new node becomes both the head and the tail
    if (list->head == NULL && list->tail == NULL) {
        list->head = newNode;
        list->tail = newNode;
    } else {
        // Otherwise, link the current tail to the new node and update the tail
        newNode->previous = list->tail;
        list->tail->next = newNode;
        list->tail = newNode;
    }
}

/* 
 * Pop the head from the list
 */
void* dequeueFromList(LinkedList* list) {
    if (list->head == NULL) {
        return NULL; // List is empty, nothing to remove
    }

    ListNode* nodeToRemove = list->head;
    void* data = nodeToRemove->data;

    // Move the head pointer to the next node
    list->head = nodeToRemove->next;

    // If the new head is NULL (list had only one item), update the tail as well
    if (list->head == NULL) {
        list->tail = NULL;
    } else {
        // Otherwise, set the previous pointer of the new head to NULL
        list->head->previous = NULL;
    }

    // Free the removed node
    free(nodeToRemove);

    return data;
}

/* 
 * Destroys contents of entire list. Leaves LinkedList struct intact.
 */
void freeListContents(LinkedList* list) {
    if (list->head == NULL) {
        // List is empty, nothing to remove
    }

    ListNode* nodeToRemove = list->head;

    while (nodeToRemove != NULL) {
        ListNode* nextNode = nodeToRemove->next;
        free(nodeToRemove);
        nodeToRemove = nextNode;
    }

}

/* 
 * Assumes list nodes contain exit_child_status. Destroys contents of entire 
 * list, including exit_child_status objects. Leaves LinkedList struct intact.
 */
void freeListContentsExitChildren(LinkedList* list) {
    if (list->head == NULL) {
        // List is empty, nothing to remove
    }

    ListNode* nodeToRemove = list->head;

    while (nodeToRemove != NULL) {
        ListNode* nextNode = nodeToRemove->next;
        free((exit_child_status *) nodeToRemove->data);
        free(nodeToRemove);
        nodeToRemove = nextNode;
    }
}

// Return the value of the front (oldest) element of the Queue without removing the element from the Queue.
void* peekFromList(LinkedList* list){
    // List is empty, nothing to remove
    if (list->head == NULL) {
        return NULL; 
    }

    return list->head->data;
} 

/* This function assume that the node in the Linked_List contains a PCB struct*/
int SearchAndRemovePCB(LinkedList* list, int pid) {
    // Check if the list is empty
    if (list == NULL || list->head == NULL) {
        return ERROR; // The list is empty or does not exist, nothing to remove
    }

    ListNode* current = list->head;

    // Iterate over the list to find the node
    while (current != NULL) {
        PCB* pcb = (PCB*)current->data; // Cast the data to PCB*
        if (pcb != NULL && pcb->pid == pid) {
            // Found the PCB to be removed
            
            // If the node is the only one in the list
            if (list->head == list->tail) {
                list->head = NULL;
                list->tail = NULL;
            } else if (current == list->head) {
                // Node is the head
                list->head = current->next;
                list->head->previous = NULL;
            } else if (current == list->tail) {
                // Node is the tail
                list->tail = current->previous;
                list->tail->next = NULL;
            } else {
                // Node is in the middle
                current->previous->next = current->next;
                current->next->previous = current->previous;
            }

            free(current); // Free the ListNode
            return 1; // Return success
        }
        current = current->next;
    }

    return ERROR; // PCB not found in the list
}

/*
 * Assumes that the input list is a list of ListNodes that contain
 * PCB structs as data. Returns the PCB in this list that has the input
 * pid. If not found, return NULL.
 */ 
PCB* SearchAndReturnPCB(LinkedList* list, int pid) {
    if (list == NULL || list->head == NULL) {
        printf("Error: The list is empty or does not exist.\n");
        return NULL; // The list is empty or does not exist
    }

    ListNode* current = list->head;

    // Iterate over the list to find the PCB with the matching pid
    while (current != NULL) {
        PCB* pcb = (PCB*)current->data; // Cast the data to PCB*

        if (pcb == NULL) {
            printf("Error: Node data is NULL, not a valid PCB.\n");
            return NULL; // Encountered invalid PCB data
        }

        if (pcb->pid == pid) {
            // Found the PCB with the specified pid
            return pcb; // Return the PCB pointer
        }

        current = current->next;
    }

    printf("Error: No PCB with pid %d found in the list.\n", pid);
    return NULL; // PCB not found
}

// Returns 1 if the list is empty, otherwise returns 0.
int IsLinkedListEmpty(LinkedList *list) {
    if (list == NULL) {
        // If the list pointer itself is NULL, treat it as empty
        return 1;
    }
    return (list->head == NULL) ? 1 : 0;
}

// Function to print the pids of the PCBs in the linked list
void printLinkedList(LinkedList* list) {
    if (list == NULL || list->head == NULL) {
        printf("The list is empty or does not exist.\n");
        return;
    }

    TracePrintf(0, "Printing linked list\n");

    ListNode* current = list->head;
    while (current != NULL) {
        PCB* pcb = (PCB*)current->data; // Cast the data to PCB*
        if (pcb != NULL) {
            TracePrintf(0, "PID: %d\n", pcb->pid);
        } else {
            TracePrintf(0, "Error: Node data is NULL, not a valid PCB.\n");
        }
        current = current->next; // Move to the next node
    }
}