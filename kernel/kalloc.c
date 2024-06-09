//内存分配器相关
// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

//xv6自己的内存分配器，用于分配物理内存给用户进程，内核栈，页表页和管道缓冲区。分配整个4096字节的页面。
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

// end代表的free memoery的起始地址 ---> end符号的值由kernel.ld链表脚本在链接过程中计算得出,然后放入了符号表中
//我们可以在c语言中通过访问到存在于符号表中的符号end来获取到end的值
extern char end[]; // first address after kernel.
                   // defined by kernel.ld.
//简单的链表节点,
struct run {
  struct run *next;
};
//内存分配器对象
struct {
  //锁和一个空闲链表
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  //初始化锁资源
  initlock(&kmem.lock, "kmem");
  //扫描物理内存，建立数据结构用来管理当前物理内存
  //此处的end和PHYSTOP分别为freeMemory区域的起始和结束内存地址
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  //内存地址对齐——确保内存地址起始为固定物理页大小的整数倍
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)// 释放内核内存
// 
// 参数:
//   pa - 要释放的物理地址指针
void
kfree(void *pa)
{
  struct run *r;

  // 检查要释放的地址是否在合法的内存范围内，并确保对齐在页面大小上，如果不合法，则直接panic
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // 使用垃圾数据填充内存以避免悬挂指针问题
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  // 将物理地址转换为run结构体指针--- 将物理页面的起始四个字节解释为run指针
  r = (struct run*)pa;

  // 获取内核内存锁
  // 保护临界区资源
  acquire(&kmem.lock);
  // 将新释放的内存块链接到自由列表的头部
  r->next = kmem.freelist;
  kmem.freelist = r;
  // 释放内核内存锁
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
    // 使用垃圾数据填充内存以避免悬挂指针问题
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