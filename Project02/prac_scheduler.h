
// this is unused struct
struct prac_scheduler {
    // mlfq
    struct queue * mlfq[4]; // L0, L1, L2, L3 (L# is priority queue)
    int global_time;
    // moq
    struct queue * moq;
    int monopolize;

};


// only procs, and level, size is used
struct queue {
    struct proc * procs[NPROC];
    int level;
    int front; // processing process is front UNUSED
    int rear; // UNUSED
    int size;
    struct spinlock lock; // Unused Not working
    //priority queue use
    // in priority queue : front = 0 and rear - is final element
    // Normal array?

};




// functions



int scheudling(void);

void priority_boosting();
int setpriority(int pid, int priority);
int setmonopoly(int pid, int password);
int monopolize();
int unmonopolize();
struct proc * find(int pid);

int isEmpty(struct queue * queue);
int isFull(struct queue * queue);
int queue_push(struct queue * queue, struct proc * proc);
struct proc* queue_pop(struct queue *  queue);


int sys_setpriority(void);
int sys_setmonopoly(void);
void sort_L3(void);
int ruunnable_check(struct queue * queue);