#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "prac_scheduler.h"

// Need To Synchronization

extern int global_time;
extern int nextpid;
// global variable, please extern it
// It was time wasting
struct prac_scheduler * my_scheduler = {
    0
};

struct proc* proc_list[12000] = {0};

// It is will be fixed. and can be fixed to L1~L3
// Too SLOW? => Sometimes Fast
// L3 Maybe need to Heap?
// Find and global boosting is slow and not synchronized
struct queue L0 = {{0}, 0, 0, 0, 0, {0}};
struct queue L1 = {{0}, 1, 0, 0, 0, {0}};
struct queue L2 = {{0}, 2, 0, 0, 0, {0}};
struct queue L3 = {{0}, 3, 0, 0, 0, {0}};

struct queue moq = {{0}, 99, 0, 0, 0, {0}};
int mono = 0; // default mono

// First Initialize
// Not used
/*
// select appropriate process and sched it
// proc.c -> this scheduling is used
// Need to check runnable
// if error occur , return 0;
// Correctly success, return 1
// 
*/
int 
scheudling(void) 
{
    struct cpu *c = mycpu();
    
    struct proc * selected; // selected proc
    struct queue * current_queue; // choose appropriate queue


    // monolize and if moq is empty, call unmonopolize
    if(mono && isEmpty(&moq)) unmonopolize();
    
    
    // monolize and not empty current queue is moq
    if(mono) {
        current_queue = &moq;
        
    }
    
    // else if mlfqs
    // after change with RUNNABLE
    else if(ruunnable_check(&L0)) current_queue = &L0;
    else if(ruunnable_check(&L1)) current_queue = &L1;
    else if(ruunnable_check(&L2)) current_queue = &L2;
    else if(ruunnable_check(&L3)) current_queue = &L3;
    
    // error occured!
    else return 0;

    // pop the current queue
    selected = queue_pop(current_queue);

    // if selected is not existed, return 0 and try again
    // not occured
    if(selected == 0) {
        // cprintf("Error 0 \n");
        return 0; // error occured!

    }

    // It is waste in mlfq, if it occurs, tray again in proc.c scheduling
    // needed to change
    if(selected->is_mlfq == 0 && current_queue->level != 99) {
        // cprintf("Error 1 \n");
        return 0; // error occured and try agian   
    }

    // if(selected->level == 99) cprintf("%d %d\n", selected->pid, selected->state);
    // If not ruunable it is pop
    // if the proc is sleeping, push agin, but priority is 0,
    // current go to priority 0!
    // selected is mlfq added
    if(selected->state != RUNNABLE) {
        // sleeping!
        if(selected->state == SLEEPING) {
            selected->priortiy = 0;
            // cprintf("current pid : %d\n", selected->pid);
            queue_push(current_queue, selected);
        }
        // cprintf("Error 2 \n");
        return 0;           
    }
    /*
    if(selected->level != current_queue->level) {
        queue_push(current_queue, selected);
        return 0;
    }
    */
    
    // basic scheduling code
    // cprintf("current pid : %d currnet queue size: %d process level: %d process state: %d\n",selected->pid, current_queue->size, selected->level, selected->state);
    // cprintf("currnet pid :  %d currnet level : %d current_queue : %d\n", selected->pid, selected->level, current_queue->level);
    c->proc = selected;
    switchuvm(selected);
    selected->state = RUNNING;

    swtch(&(c->scheduler), selected->context);
    switchkvm();
    
    // Process is done running for now.
    // It should have changed its p->state before coming back.
    c->proc = 0;
    
    // push is closed to selected
    
     
    // push again
    
    // if(selected->is_mlfq == 1) is it push again is need? => not need
    // push again in to selected proc to appropriate queue
    queue_push(current_queue, selected);
    // cprintf("%d\n", L3.size);
    return 1;
}

// pop all L0 L1 L2 L3 not moq
// => Just change level and processing_time
// pop all elements have many errors
void 
priority_boosting(void) 
{
   
    int i = 0;
    struct proc * proc;
    for(i = 1; i < nextpid; i++) {
        proc = proc_list[i];
        if(!proc) continue;
        if(proc->is_mlfq == 0) continue;
        proc->level = 0;
        proc->processing_time = 0;
    }

    // yield();
    
    // struct proc * proc;
    
    // L0 is only need to update processing time
    // L1 ~ L3
    struct queue * currnet_queue;
    currnet_queue = &L1;
    while(!isEmpty(currnet_queue)) {
        proc = queue_pop(currnet_queue);
        if(!proc) break;
        queue_push(&L0, proc);
    }
    
    currnet_queue = &L2;
    while(!isEmpty(currnet_queue)) {
        proc = queue_pop(currnet_queue);
        if(!proc) break;
        queue_push(&L0, proc);
    }
    
    currnet_queue = &L3;
    while(!isEmpty(currnet_queue)) {
        proc = queue_pop(currnet_queue);
        if(!proc) break;
        queue_push(&L0, proc);
    }
    
}

// find process in the Queue L1, L2, L3
int 
setpriority(int pid, int priority) 
{
    if(priority < 0 || priority > 10) {
        return -2;
    }
    
    struct proc* proc = find(pid);
    
    if(!proc) {
        return -1;
    }

    proc->priortiy = priority;

    // test code
    
    // It needs to sort
    sort_L3();
    return 0;
}

// setmonopoly and push it to moq
int 
setmonopoly(int pid, int password) 
{
    if(password != 2020030819) return -2;
    
    struct proc* proc = find(pid);
    
    if(proc == 0) return -1;
    
    
    // is in mlfq
    if(proc->is_mlfq == 1) {
        proc->is_mlfq = 0;
        proc->level = 99;
        queue_push(&moq, proc);
    }

   
    yield();
    // return my_scheduler->moq->size;
    return (&moq)->size;
}

// call monopolize
int 
monopolize() 
{
    // my_scheduler->monopolize = 1;
    mono = 1;
    // cprintf("monopolize!\n");
    // yield();
    return 1;
}

// call unmonopolize
int 
unmonopolize() 
{
    // my_scheduler->monopolize = 0;
    mono = 0;
    global_time = 0;
    // cprintf("unmonopolize!\n");
    // yield();
    return 0;
}



// Support Functions

// need to implement find
struct proc * 
find(int pid) 
{
    /*
    int i;
    struct queue * current_queue;


    current_queue = &L0;
    for(i = 0; i < current_queue->size; i++) {
        cprintf("current : %d Q : %d : \n", current_queue->procs[i]->pid, current_queue->level);
        if(pid == current_queue->procs[i]->pid)
            return current_queue->procs[i];
    }

    current_queue = &L1;
    for(i = 0; i < current_queue->size; i++) {
        cprintf("current : %d Q : %d : \n", current_queue->procs[i]->pid, current_queue->level);
        if(pid == current_queue->procs[i]->pid)
            return current_queue->procs[i];
    }

    current_queue = &L2;
    for(i = 0; i < current_queue->size; i++) {
        cprintf("current : %d Q : %d : \n", current_queue->procs[i]->pid, current_queue->level);
        if(pid == current_queue->procs[i]->pid)
            return current_queue->procs[i];
    }
    
    current_queue = &L3;
    for(i = 0; i < current_queue->size; i++) {
        cprintf("current : %d Q : %d : \n", current_queue->procs[i]->pid, current_queue->level);
        if(pid == current_queue->procs[i]->pid)
            return current_queue->procs[i];
    }

    current_queue = &moq;
    for(i = 0; i < current_queue->size; i++) {
        cprintf("current : %d Q : %d : \n", current_queue->procs[i]->pid, current_queue->level);
        if(pid == current_queue->procs[i]->pid)
            return current_queue->procs[i];
    }
    */

   // just find with proc_list, finding in all queue have many errors
    if(proc_list[pid]) return proc_list[pid];


    return 0;
}

// empty and full
int 
isEmpty(struct queue * queue) 
{
    return queue->size == 0;
}

int 
isFull(struct queue * queue) 
{
    return queue->size == NPROC;
}

// push align to proc level
// . change to ->
int 
queue_push(struct queue * queue, struct proc * proc) 
{

    int i;

    int level = proc->level;

    // not to close memory 0!
    struct queue * current_queue;
    
    if(level == 99) current_queue = &moq;
    // after change with RUNNABLE
    else if(level == 0) current_queue = &L0;
    else if(level == 1) current_queue = &L1;
    else if(level == 2) current_queue = &L2;
    else if(level == 3) current_queue = &L3;
    else return 0;

    
    // L3 use priority
    
    if(level != 3) {
        current_queue->procs[current_queue->size] = proc;
        current_queue->size++;
    }
    
    
    // use priority?
    else if(level == 3) {
        for(i = current_queue->size; i > 0; i -= 1) {
            if(i > 0 && current_queue->procs[i-1]->priortiy < proc->priortiy) 
                current_queue->procs[i] = current_queue->procs[i-1];
            else
                break; 
        }
        current_queue->procs[i] = proc;
        current_queue->size++;
    }
    
    // error
    else {
        return 0;
    }
    
    return 1;
}

// pop => queue level
// queue is not used
struct proc* 
queue_pop(struct queue *queue) 
{
    if(isEmpty(queue)) return 0;
   
    struct proc* result;
    int i;
    struct queue *  currnet_queue = queue;
    result = currnet_queue->procs[0];
    
    for(i = 0; i < currnet_queue->size -1; i++)
        currnet_queue->procs[i] = currnet_queue->procs[i + 1];
    
    /*
    Need to Synchronized
    if(result->pid != currnet_queue->procs[0]->pid) 
        cprintf("hre\n");
    */
    currnet_queue->size -= 1;
    
    return result;
}

// when set priority
void 
sort_L3(void) 
{
    int i, j;
    
    struct proc * temp;

    struct queue * current_queue = &L3;
    int size = current_queue->size;

    for(i = 0; i < size - 1; i++) {
        for(j = i + 1; j < size; j++) {
            if(current_queue->procs[j]->priortiy > current_queue->procs[i]->priortiy) {
                temp = current_queue->procs[j];
                current_queue->procs[j] = current_queue->procs[i];
                current_queue->procs[i] = temp;
            }
        }
    }
}
// Wrapper Functions

int 
sys_setpriority(void) 
{
    int pid, priority;
    
    if(argint(0, &pid) < 0)
        return -1;
    
    if(argint(1, &priority) < 0)
        return -1;
    return setpriority(pid, priority);

}

int 
sys_setmonopoly(void) 
{
    int pid, password;
    if(argint(0, &pid) < 0)
        return -1;
    if(argint(1, &password) < 0)
        return -1;
    return setmonopoly(pid, password);
}

int 
ruunnable_check(struct queue * queue) 
{
    if(isEmpty(queue)) return 0;

    
    int i;
    for(i = 0; i < queue->size; i++) {
        if(queue->procs[i]->state == RUNNABLE)
            return 1;
    }
    

   
    return 0;
}
