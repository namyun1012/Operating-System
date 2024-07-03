#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

int
myfunction(char *str) {
    cprintf("%s\n", str);
    return 0xABCDABCD;
}


//Wrapper for my_syscall
int
sys_myfunction(void) 
{
    char *str;
    if(argstr(0, &str) < 0)
        return -1;

    return myfunction(str);
}

void sys_yield(void) {
    yield();
}



int sys_getlev(void) {
    
    return myproc()->level;
}
