// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
//nts 循环请求失败的次数，n是请求获取锁的次数


void freerange(void *pa_start, void *pa_end,uint cpu_id);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmems[NCPU];

void
kinit()
{
  char* start_malloc = end;
  int length = ((uint64)PHYSTOP - (uint64)end)/NCPU;

  for(int i=0;i<NCPU;i++)
  {
    char name[32];
    name[0]='k';
    name[1]='m';
    name[2]='e';
    name[3]='m';
    name[4]='s';
    name[5]='[';
    name[6]='0'+i;
    name[7]=']';
    name[8]=0;

    initlock(&(kmems[i].lock),name);
    freerange(start_malloc,start_malloc+length,i);
    start_malloc+=length;
  }
}

void
freerange(void *pa_start, void *pa_end,uint cpu_id)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    // add the real operator of free op
    char* pa = p;
    struct run *r;

    if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
      panic("kfree");

    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run*)pa;

    acquire(&kmems[cpu_id].lock);
    r->next = kmems[cpu_id].freelist;
    kmems[cpu_id].freelist = r;
    release(&kmems[cpu_id].lock);
    
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  push_off();
  int id = cpuid();
  pop_off();

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmems[id].lock);
  r->next = kmems[id].freelist;
  kmems[id].freelist = r;
  release(&kmems[id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  int id;
  push_off();
  id = cpuid();
  pop_off();


  struct run *r;
  acquire(&kmems[id].lock);
  r = kmems[id].freelist;

  if(r){
    kmems[id].freelist = r->next;
    release(&kmems[id].lock);
  }
  else{
    release(&kmems[id].lock);

    for(int i=0;i<NCPU;i++){
      if(i==id)continue;
      acquire(&kmems[i].lock);
      struct run* halfptr = kmems[i].freelist;
      r = halfptr;
      struct run* ptr = kmems[i].freelist;

    
      if(halfptr){
        //有空闲的页
        while (ptr)
        {

          ptr = ptr->next;
          if(ptr){
            ptr = ptr->next;
            halfptr = halfptr->next;
            }
        }

        acquire(&kmems[id].lock);
        kmems[id].freelist = r->next;
        release(&kmems[id].lock);
        //error occur
        kmems[i].freelist = halfptr->next;

        halfptr->next = 0;
        release(&kmems[i].lock);
        break;
      }else{
        //该链表也没有空闲的页
        release(&kmems[i].lock);      
        continue;
      }
    }

  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
    
  return (void*)r;


}
