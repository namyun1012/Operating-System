#include "types.h"
#include "stat.h"
#include "user.h"


int main(int argc, char *argv[]) {

    int pid = fork();
    int i = 0;
    
    
    for(i = 0; i < 100; i++) {
        if(pid == 0) {
            printf(1, "Child %d %d \n" ,getpid(), getlev());  
        }
        else {
            printf(1, "Parent %d %d\n",getpid(), getlev());
        }
    }

    wait();
    exit();
}