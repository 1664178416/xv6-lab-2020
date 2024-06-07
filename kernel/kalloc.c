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

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// COW reference count
struct 
{
  uint8 ref_cnt;
  struct spinlock lock;
} cow[(PHYSTOP - KERNBASE) >> 12];

//引用+1
void increfcnt(uint64 pa){
  if(pa < KERNBASE) return;
  pa = (pa - KERNBASE) >> 12;
  acquire(&cow[pa].lock);
  ++cow[pa].ref_cnt;
  release(&cow[pa].lock);
}

//引用减1
uint8 decrefcnt(uint64 pa){
  uint8 ret;
  if(pa < KERNBASE) return 0;
  pa = (pa - KERNBASE) >> 12;
  acquire(&cow[pa].lock);
  ret = --cow[pa].ref_cnt;
  release(&cow[pa].lock);
  return ret;
}
//kinit() 函数主要作用就是对物理内存空间中未使用的部分以物理页划分调用 kfree() 将其添加至 kmem.freelist 中
void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  // //此处啥也没干的时候计数值会是0，那么调用kfree就会引发错误变成255导致清除不了
  // increfcnt((uint64)p); //开始时引用要+1,傻子，肯定是在里面进行增加引用那。。。
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    increfcnt((uint64)p); //开始时引用要+1
    kfree(p);
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

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  if(decrefcnt((uint64)pa)){ //释放时引用-1
    return;
  }

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  // if(decrefcnt((uint64)pa)){ //释放时引用-1,不能放在这里啊！！！上面都已经有清楚的动作了，所以必须在清除动作之前进行限制
  //   return;
  // }

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock); 
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  //空闲链表法，从头部拿一个空闲页表出来
  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);
  increfcnt((uint64)r); //开始时引用要+1
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
