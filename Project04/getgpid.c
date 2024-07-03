#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

// see proc.h
int 
sys_getgpid() {
    struct proc* pproc = myproc()->parent;
    struct proc* gproc = pproc->parent;
    
    return gproc->pid;
}

//Wrapper
