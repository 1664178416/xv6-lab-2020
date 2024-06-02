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

  acquire(&kmem.lock);
  r = kmem.freelist; // 获取空闲列表的根节点
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

// freebytes 函数用于计算并更新系统中空闲内存的总字节数。
// 它接受一个 uint64 类型的指针 dst，该指针将被用于存储计算得到的空闲字节数。
// 该函数不返回任何值。
void
freebytes(uint64 *dst){
  // 将 dst 指向的内存区域清零，表示初始空闲字节数为0。
  *dst = 0;
  
  // 获取内存管理结构 kmem 的空闲列表指针。
  struct run *p =kmem.freelist; 
  // 加锁以保护对 kmem 结构的访问，确保并发安全。
  acquire(&kmem.lock); 
  
  // 遍历空闲列表中的每个内存块，累加它们的大小到 dst。
  while(p){
    // 累加每个内存块的大小（PGSIZE）到 dst。
    // 统计空闲页数，每次累加上页大小 PGSIZE ，就是空闲的字节数
    *dst += PGSIZE;
    // 移动到下一个内存块。
    p = p->next;
  }
  
  // 遍历完成后，释放对 kmem 结构的锁。
  release(&kmem.lock);
}