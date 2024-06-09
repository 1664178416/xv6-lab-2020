// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};


//kmem存储的是空闲的物理页的链表，修改成NCPU，为每个CPU维护一个空闲链表
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

void
kinit()
{
  char lockname[8];
  /* 初始化内核内存锁，确保内存管理的线程安全 */
  for(int i = 0;i < NCPU;i++){
    //把每个锁名字初始化为kmem_0,kmem_1,kmem_2...
    snprintf(lockname,sizeof(lockname),"kmem_%d",i);
    initlock(&kmem[i].lock, lockname);
  }
  
   /* 将从end到PHYSTOP之间的内存区域标记为可用，供内核动态分配使用 */
  freerange(end, (void*)PHYSTOP);  //PHYSTOP = KERNBASE + 128*1024*1024，代表内核和用户页的最大地址
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  //使用cpuid()和它返回的结果时必须关中断，否则如果当前任务因为时间片到期切换到其他CPU上运行，那么先前获取的cpuId就不正确了
  push_off(); //关中断
  int id = cpuid();
  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
  pop_off(); //开中断
  // acquire(&kmem.lock);
  // r->next = kmem.freelist;
  // kmem.freelist = r;
  // release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off(); //关中断

  int id = cpuid();
  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r)
    kmem[id].freelist = r->next;
  else{
    int antid; //其他的cpuid
    //遍历所有的cpu空闲链表
    for(antid = 0; antid < NCPU;antid++){
      if(antid == id)continue;;
      acquire(&kmem[antid].lock);
      r = kmem[antid].freelist;
      if(r){
        kmem[antid].freelist = r->next;
        release(&kmem[antid].lock);
        break;
      }
      release(&kmem[antid].lock);
    }
    
  }
  release(&kmem[id].lock);
  pop_off(); //开中断
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
