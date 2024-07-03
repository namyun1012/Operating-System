// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld
void incr_refc(uint pageNum);
void decr_refc(uint pageNum);
int get_refc(uint pageNum);

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;

  // added code
  // page directory 10 , page  table 10, page offset 12
  // memlayout.h PHYSTOP and P2V V2P
  // so top address >> 12
  uint count[(PHYSTOP >> 12) + 1]; // number of pages, physical top address / PGSIZE? (2^12) offset is 12 bit
} kmem;



// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;
  uint pa;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  
  // added code 
  // kfree is in several place
  // maybe revise need
  
  pa = V2P((uint) v);

  // error occur here?
  // decr_refc
  if(get_refc(pa >> 12) >=  1) {
    decr_refc(pa >> 12);
  } 
  
  // check
  // if it is 0 execute
  if(get_refc(pa >> 12) != 0) {
    // panic("it can not free");
    return ;
  }
  

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = (struct run*)v; // v and r are same?
  r->next = kmem.freelist;
  kmem.freelist = r;
  if(kmem.use_lock)
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  struct run *r;
  uint pa;

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  

  if(r) {
    kmem.freelist = r->next;
    
    // added code
    pa = V2P((uint)r); // change it
    kmem.count[pa >> 12] = 1; // set to 1
  }

  if(kmem.use_lock)
    release(&kmem.lock);
  return (char*)r;
}

//added functions

void incr_refc(uint pageNum) {
  if(kmem.use_lock)
    acquire(&kmem.lock);
  
  kmem.count[pageNum]++;
  
  if(kmem.use_lock)
    release(&kmem.lock);
}

void decr_refc(uint pageNum) {
  if(kmem.use_lock)
    acquire(&kmem.lock);
  
  kmem.count[pageNum]--;
  
  if(kmem.use_lock)
    release(&kmem.lock);
}

int get_refc(uint pageNum) {
  if(kmem.use_lock)
    acquire(&kmem.lock);
  
  int temp = kmem.count[pageNum];
  
  if(kmem.use_lock)
    release(&kmem.lock);

  return temp;
}

// free pages number
int countfp(void) {
  struct run *r;
  uint freepages = 0;
  
  if(kmem.use_lock)
    acquire(&kmem.lock);

  r = kmem.freelist;
  while(r) {
    r = r->next;
    freepages++;
  }

  if(kmem.use_lock)
    release(&kmem.lock);

  return freepages;
}

int sys_countfp(void) {
  return countfp();
}