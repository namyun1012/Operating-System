#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

// added
#include "elf.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
int nexttid = 1;

extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void recollect_memory(struct proc * p);
void execthread(struct proc * curproc);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  // added code it is process
  p->master_thread = p; // myself if process
  p->worker = 0; // it is not thread
  p->tid = -1; // tid is not used

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
// sbrk sys call
// pgdir is already shared.
// need sharing all?
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();
  struct proc * p;
  
  acquire(&ptable.lock);
  
  sz = curproc->master_thread->sz; //
  if(n > 0){
    if((sz = allocuvm(curproc->master_thread->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->master_thread->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  // curproc->master_thread->sz = sz;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->pid != curproc->pid) continue;
    p->sz = sz;
  }

  release(&ptable.lock);

  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();
  // curproc = curproc->master_thread;
  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);
  
  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // all thread recollect
  // if worker call, master is recollect too.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    // select thread
    if(p->pid == curproc->pid && p->tid != curproc->tid) {
    // recollect files
    for(fd = 0; fd < NOFILE; fd++){
      if(p->ofile[fd]){
        fileclose(p->ofile[fd]);
        p->ofile[fd] = 0;
      }  
    } 
    /*
    accquire error occur!
    begin_op();
    iput(p->cwd);
    end_op();
    */
    p->cwd = 0;
    recollect_memory(p);
    }
  }
  
  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's xjob
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
// chan is not sharable variable, not to change
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;
  int fd;
  // struct proc * master = p->master_thread;
  acquire(&ptable.lock);
  
  // kill all worker threads
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    
    if(p->pid == pid && p->worker == 1) {
        for(fd = 0; fd < NOFILE; fd++){
          if(p->ofile[fd]){
            fileclose(p->ofile[fd]);
            p->ofile[fd] = 0;
        }  
      } 
    /*
    error occur
    begin_op();
    iput(p->cwd);
    end_op();
    */
      p->cwd = 0;
      recollect_memory(p);
    }     
  }

  // worker thread is always after master 
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

// may be added System Calls
// fork and exec
int thread_create(thread_t * thread, void * (*start_routine)(void *), void * args) {
  // fork
  
  int i;
  struct proc *np;
  struct proc *curproc = myproc();
  struct proc *master = curproc->master_thread;
  uint sz, sp, ustack[3 + MAXARG + 1];
  struct proc * p;
  // pde_t *pgdir;


  // Allocate thread kernal stack
  if((np = allocproc()) == 0){
    return -1;
  }
  
  acquire(&ptable.lock);
  
  // use master's pid. 
  nextpid -= 1;
  
  
  // it is worker thread and master thread is process
  np->worker = 1;
  np->master_thread = master;
  
  
  // pid is master->pid and make tid 
  np->pid = master->pid;
  np->tid = nexttid++;
  *thread = np->tid;

  *np->tf = *master->tf; // trap frame is copied
  np->pgdir = master->pgdir; // pgdir is shared.
  np->parent = master->parent; // parent is curproc's parent
  
  // file is shared
  // but if change the code panic occur!
  for(i = 0; i < NOFILE; i++)
    if(master->ofile[i])
      np->ofile[i] = filedup(master->ofile[i]);
  np->cwd = idup(master->cwd);

  safestrcpy(np->name, master->name, sizeof(master->name));

  // pid = np->pid;

  //exec 
  // Load Program is Not need
  // First, ELF is not need, just use master
  // user stack is needed.
  // first is guard and second is user stack
  np->prev_sz = master->sz; // save prev size 
  master->sz += 2 * PGSIZE; // Guard page and user stack page is added
  
  sz = np->prev_sz; // sz = size of memory byte
  if((sz = allocuvm(np->pgdir, sz, sz + 2*PGSIZE)) == 0) {
    cprintf("allocated failed\n");
    return -1;
  }

  clearpteu(np->pgdir, (char*)(sz - 2*PGSIZE)); // thread user ~ guard stack cleaning
  sp = sz;
  // No argv
  ustack[3] = 0;

  ustack[0] = 0xffffffff;
  ustack[1] = (uint) args;
  ustack[2] = sp; // there is no argv pointer
  
  // not exist argv
  sp -= 16; // 4 * 4, argv is not existed

  //ustack >> stack
  if(copyout(np->pgdir, sp, ustack, 16) < 0) {
    cprintf("copy out error!\n");
    return -1;
  }
  
  // start routine
  np->thread_sz = sz; // save
  np->sz = sz; // set to sz
  np->tf->eip = (uint)start_routine; //elf.entry
  np->tf->esp = sp;
  // np->retval = args; // args in retval? Confuse it
  // memory size is same? 
  //pgdir is already
  

  // all worker thread sz is same
  // if not, sbrk error occur
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->pid != curproc->pid) continue;
    p->sz = sz;
  }
  

  np->state = RUNNABLE;

  release(&ptable.lock);
  return 0;
}

// first, copy exit and add retval?
void thread_exit(void *retval) {
  
  struct proc *curproc = myproc(); // current thread
  // struct proc *p;
  int fd;
  /*
  is it Need?
  if(curproc->worker == 0) {
    panic("This is not Worker Thread, it call thread_exit\n");
    return ;
  }
  */
  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }
  
  begin_op();
  iput(curproc->cwd);
  end_op();
  
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Master thread might be sleeping in wait(). Not parent
  wakeup1(curproc->master_thread);

  // Pass abandoned children to init. thread can have child so this code need
  
  struct proc * p;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }
  
  // cur threads's retval to retval
  // join get this value

  // I confuse it. curproc->retval = retval is correct not retval = curproc->retval;
  curproc->retval = retval;
  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");

}

// maybe wait?
// tid thread exit and resource check
int thread_join(thread_t thread, void ** retval) {
  struct proc *p;
  struct proc *curproc = myproc();
  // int tid;
  // cprintf("join call\n");

  // first check master thread
  /* is It need?
  if(curproc->worker == 1) {
    panic("Worket Thread call join\n");
    return -1;
  }
  */
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      
      // first, tid check
      if(p->tid != thread)
        continue;

      // thread is call thread exit
      // find the tid
      if(p->state == ZOMBIE){
        // Found the thread
        *retval = p->retval; // this is first
        
        kfree(p->kstack);
        p->kstack = 0;
        // freevm(p->pgdir); pgdir is shared
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;

        p->master_thread = 0;
        
        // send to retval
        // only remove thread user stack and gurad stack! 
        deallocuvm(p->pgdir, p->thread_sz, p->prev_sz);
        
        // added
        p->prev_sz = 0;
        p->thread_sz = 0;
        p->sz = 0;
        p->tid = 0;
        p->worker = 0;
        p->master_thread = 0;
        p->retval = 0; // set cur thread retval to 0
        
        release(&ptable.lock);
        return 0;
      }
    }

    // No point waiting if we don't have any children.
    if(curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }

}
// for using exec
// eliminating all other thread (including master)
void execthread(struct proc * curproc) {
  
  struct proc * p;
  int fd;
  
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->pid == curproc->pid && p->tid != curproc->tid) {

    for(fd = 0; fd < NOFILE; fd++){
      if(p->ofile[fd]){
        fileclose(p->ofile[fd]);
        p->ofile[fd] = 0;
      }  
    } 
    /*
    accquire error occur!
    begin_op();
    iput(curproc->cwd);
    end_op();
    */
    p->cwd = 0;
    recollect_memory(p);
    }
  }
  release(&ptable.lock);
}

// to simply code use to exec, exit, kill
void recollect_memory(struct proc * p) {
        kfree(p->kstack);
        p->kstack = 0;
        // freevm(p->pgdir); pgdir is shared
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;

        p->master_thread = 0;
        
        // send to retval
        // only remove thread user stack and gurad stack! 
        deallocuvm(p->pgdir, p->thread_sz, p->prev_sz);
        
        // added
        p->prev_sz = 0;
        p->thread_sz = 0;
        p->sz = 0;
        p->tid = 0;
        p->worker = 0;
        p->master_thread = 0;
        p->retval = 0;


}