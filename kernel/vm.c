#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "proc.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

/*
 * create a direct-map page table for the kernel.
 */

void uvmmap(pagetable_t pagetable,uint64 va,uint64 pa,uint64 sz,int perm){
  //和kvmmap区别就在于mappages传入的pagetable不是固定的kernel_pagetable
  if(mappages(pagetable,va,sz,pa,perm) != 0)
    panic("uvmmap");
}

//此处可以参照下面的kvminit()函数，将I/O设备映射到用户空间的进程内核页表中
pagetable_t
proc_kpt_init(){
  pagetable_t kernelpt = uvmcreate();
  if (kernelpt == 0) return 0;
  uvmmap(kernelpt, UART0, UART0, PGSIZE, PTE_R | PTE_W);
  uvmmap(kernelpt, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);
  uvmmap(kernelpt, CLINT, CLINT, 0x10000, PTE_R | PTE_W);
  uvmmap(kernelpt, PLIC, PLIC, 0x400000, PTE_R | PTE_W);
  uvmmap(kernelpt, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);
  uvmmap(kernelpt, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);
  uvmmap(kernelpt, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
  return kernelpt;
}

void
kvminit()
{
  //为最高一级page directory分配物理page （调用 kalloc就是分配物理page)
  kernel_pagetable = (pagetable_t) kalloc();
  //将这段你内存初始化为0
  memset(kernel_pagetable, 0, PGSIZE);

  //通过kvmmap函数将每个I/O设备映射到内核
  //映射UART0设备，允许读写访问
  //UART寄存器
  // uart registers
  kvmmap(UART0, UART0, PGSIZE, PTE_R | PTE_W);
  
  //映射virtio0设备，用于访问虚拟磁盘，拥有读写权限
  //virtio 内存映射磁盘接口
  // virtio mmio disk interface
  kvmmap(VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);
   // 映射 CLINT（集群互连计时器）设备，用于定时中断和核心间同步。
  // CLINT
  kvmmap(CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // 映射 PLIC（外部中断控制器）设备，用于处理外部中断。
  // PLIC
  kvmmap(PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // 映射内核代码段（从KERNBASE到etext），设置为可执行和只读
  //KERNBASE 是内核虚拟地址空间的起始地址，而 etext 是内核可执行代码段的结束地址。
  //通过调用 kvmmap 函数，它将内核的代码部分映射到虚拟地址空间，赋予该区域读和执行的权限（PTE_R | PTE_X）。
  //这意味着内核的指令部分可以被安全地执行，但不允许修改，这样的设置是为了保证内核的稳定性和安全性，防止运行时意外修改代码段。
  // map kernel text executable and read-only.
  kvmmap(KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // 映射内核数据段及内核所使用的内存（从etext到PHYSTOP），设置为可读写
  // map kernel data and the physical RAM we'll make use of.
  kvmmap((uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  //映射跳板代码，用于陷阱入口/退出，即用于在内核和用户模式之间切换，放于内核中的最高虚拟地址处
  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}


// Switch h/w page table register to the kernel's page table,
// and enable paging.
/**
 * 初始化处理器的页表
 * 
 * 本函数的目的是在处理器启动初期，设置虚拟地址到物理地址的映射关系，使用的是内核页表。
 * 这是通过修改系统地址转换寄存器（SATP）的值来实现的。修改SATP后，还需要确保处理器
 * 的缓存和内存中的数据视图一致，因此跟着一条内存屏障指令sfence_vma。
*/
void
kvminithart()
{
  // 设置SATP寄存器，使用内核页表
  w_satp(MAKE_SATP(kernel_pagetable));
  
  // 确保内存和缓存中的数据视图一致
  sfence_vma();
}

void 
proc_inithart(pagetable_t pagetable){
  w_satp(MAKE_SATP(pagetable));
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
//该函数只在启动阶段使用，用于向内核页表中添加映射条目，不会刷新TLB（快表）或启用分页。
void
//四个参数：虚拟地址、物理地址、大小、读写权限
kvmmap(uint64 va, uint64 pa, uint64 sz, int perm)
{
  //mappages函数负责完成具体映射条目的建立工作
  if(mappages(kernel_pagetable, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// translate a kernel virtual address to
// a physical address. only needed for
// addresses on the stack.
// assumes va is page aligned.
/*
 * 函数名称：kvmpa
 * 函数功能：将虚拟地址转换为物理地址(只针对全局内核页表进行)
 * 参数说明：
 *   va：需要转换的虚拟地址
 * 返回值：
 *   转换后的物理地址
 * 
 * 函数实现的详细说明：
 * 1. 计算虚拟地址的页内偏移量。
 * 2. 通过页表查询给定虚拟地址对应的页表项。
 * 3. 如果页表项不存在，则调用panic，表示系统出现了严重错误。
 * 4. 如果页表项存在但未被激活（未映射到物理页面），则同样调用panic，表示虚拟地址无效。
 * 5. 从有效的页表项中提取出物理地址。
 * 6. 将物理地址与虚拟地址的页内偏移量相加，得到最终的物理地址。
 */
uint64
kvmpa(uint64 va)
{
  /* 计算虚拟地址的页内偏移量 */
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;
  
  /* 查询给定虚拟地址在内核页表中的页表项 */
  pte = walk(myproc()->kernalpt, va, 0); //拿到当前进程的内核页表，并查询物理地址
  /* 如果页表项不存在，则调用panic */
  if(pte == 0)
    panic("kvmpa");
  /* 如果页表项存在但未被激活，则同样调用panic */
  if((*pte & PTE_V) == 0)
    panic("kvmpa");
  /* 从有效的页表项中提取出物理地址 */
  pa = PTE2PA(*pte);
  /* 返回物理地址与页内偏移量相加的结果 */
  return pa+off;
}
// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0)
      panic("uvmunmap: not mapped");
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

//仿照freewalk进行打印的编写
void _vmprint(pagetable_t pagetable,int level){
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if(pte&PTE_V) { //如果页表有效
      printf("..");
      for(int j = 0;j < level;j++){
        printf(" ..");
      }
      printf("%d: pte %p pa %p\n",i,pte,PTE2PA(pte));

      //如果该节点不是叶子节点，递归打印其子节点
      if((pte & (PTE_R|PTE_W|PTE_X)) == 0){
        uint64 child = PTE2PA(pte);
        _vmprint((pagetable_t)child,level+1);
      }
    }
  }
}

int vmprint(pagetable_t pagetable){
  printf("page table %p\n",pagetable);
  _vmprint(pagetable,0);
  return 0;
}


// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}
