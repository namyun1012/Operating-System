#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "prac_scheduler.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

int global_time = 0;
// I added
extern struct prac_scheduler * my_scheduler;
extern struct queue L0;
extern struct queue L1;
extern struct queue L2;
extern struct queue L3;

extern struct queue moq;
extern int mono; // default mono



void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);
  SETGATE(idt[128], 1, SEG_KCODE<<3, vectors[128], DPL_USER); // Add code,

  initlock(&tickslock, "time");

}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }
  
  // Add code
  if(tf->trapno == 128) {
    cprintf("interrupt 128 is called!\n");
    exit();
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  // Current Timeout (every tick)

  // Push is on trap.c and pop is on every scheduling

  // Running and tick is increased 1
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER) {
      
      
      // for priority boosting and only boost in mlfq 
      myproc()->processing_time += 1;
      global_time++;
      
      // struct proc* proc = myproc();
      // my_scheduler->global_time += 1;
      
      // cprintf("%d", my_scheduler->global_time);
      // for temp
      // struct proc* proc = myproc();
      // push is in the prac_scheduler.c
      // mlfq
      if(myproc()->is_mlfq && mono == 0) {
        // level is only used here
        int proc_level = myproc()->level;
        int quantum = proc_level * 2 + 2;
        
        
        
        // processing time is over
        if(myproc()->processing_time >= quantum) {
          
          // if level is 0
          if(proc_level == 0) {
            
            // before yield we pop it and push it to another.
            if(myproc()->pid % 2 == 0) {
              myproc()->level = 2;
              myproc()->processing_time = 0;
              
              // queue_push(&L2, proc);
              yield();
            }

            // L1
            else {
              myproc()->level = 1;
              myproc()->processing_time = 0;
              
              // queue_push(&L1, proc);
              yield();
              
            }
          
          }


          else if(proc_level == 1) {
            myproc()->level = 3;
            myproc()->processing_time = 0;
            yield();
            
          }

          else if(proc_level == 2) {
            myproc()->level = 3;
            myproc()->processing_time = 0;
            yield();
          }

          // need to priority and push
          else if(proc_level == 3) {
            if(myproc()->priortiy > 0) {
              myproc()->priortiy -= 1;
            }

            myproc()->processing_time = 0;
            yield();
          }
        }

        // global priority boost here
        if(global_time >= 100) {
          // cprintf("priority boosting_start\n");
          global_time = 0;
          priority_boosting();
          
        }
      } 
      
      // moq is not RR
      

      
      // basic code is only here
      // yield();


    }
  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
