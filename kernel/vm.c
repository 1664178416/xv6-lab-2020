//虚拟内存、页表相关函数实现
#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

/*
 * create a direct-map page table for the kernel.
 */
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

/*
 * 本函数用于在虚拟内存管理系统中设置当前处理器的页表。
 * 它通过使用w_satp函数将指定的页表设置为系统的页地址转换寄存器（SATP）的值，
 * 从而使得处理器在后续的内存访问中使用新的页表。
 * 使用sfence_vma函数确保内存访问顺序的正确性，即保证对页表的写入操作在后续的内存访问之前完成。
 * satp的作用是存放根页表页在物理内存中的地址，同时还包含了页表的位数。
 */
// kvminithart() 函数通过设置 SATP 寄存器，告诉内存管理单元（MMU）使用新的页表 kernel_pagetable 进行地址翻译。

// 在这条指令执行之前，可能没有可用的页表，也就没有地址翻译。这意味着，程序计数器（PC）指向的是物理地址。

// 当 w_satp(MAKE_SATP(kernel_pagetable)); 这条指令执行后，SATP 寄存器被设置为新的页表，这意味着 MMU 会开始使用新的页表进行地址翻译。程序计数器（PC）增加 4（因为这是 RISC-V 架构的指令长度），指向下一条指令的地址。

// 然后，当下一条指令被执行时，程序计数器（PC）指向的地址会被新的页表翻译。这意味着，PC 指向的不再是物理地址，而是虚拟地址。这个虚拟地址会被新的页表翻译成物理地址，然后从物理内存中取出指令并执行。
void
kvminithart()
{
  /* 设置系统的页地址转换寄存器（SATP） */
  w_satp(MAKE_SATP(kernel_pagetable));
  
  /* 确保内存访问顺序的正确性 */
//   RISC-V有一个指令sfence.vma，用于刷新当前CPU的TLB。

// xv6在重新加载satp寄存器后，在kvminithart中执行sfence.vma，并在返回用户空间之前在用于切换至一个用户页表的trampoline代码中执行sfence.vma (*kernel/trampoline.S*:79)。

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
//遍历页表得到虚拟地址对应的最后一级页表中的页表项
/**
 * 遍历页表以查找给定虚拟地址（va）对应的物理页表条目（PTE）。
 * 如果必要，分配新的页表页面，并更新页表以反映分配。
 * 
 * pagetable 页表的根目录。
 * va 要查找的虚拟地址。
 * alloc 是否允许分配新的页表页面。
 * 返回指向找到的PTE的指针，如果无法找到或分配失败，则返回NULL。
 * 
 * 注意：该函数处理多级页表的遍历和分配，是实现虚拟内存管理的关键部分。
 */
//参数详情: 根页表地址,虚拟地址,发生缺页异常时,当页面项指向的页表页还没有加载时，是否需要创建新的页表页   

// 对于walk函数，在写完SATP寄存器之后，代码是通过page table将虚拟地址翻译成了物理地址，但是这个时候SATP已经被设置了，得到的物理地址不会被认为是虚拟地址吗？

// walk函数在设置完SATP寄存器后，还能工作的原因是，内核设置了虚拟地址等于物理地址的映射关系，这里很重要，因为很多地方能工作的原因都是因为内核设置的地址映射关系是相同的。

pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  // 检查虚拟地址是否超出最大范围，如果是，则引发panic。
  if(va >= MAXVA)
    panic("walk");

  // 从二级页表开始，逐级向下遍历页表。
  for(int level = 2; level > 0; level--) {
    // 根据当前虚拟地址和页表级别计算页表项的索引。
    pte_t *pte = &pagetable[PX(level, va)];

    // 如果当前页表项有效，则更新pagetable指向对应的物理页表，也就是得到页表项指向的页表的基地址
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      // 如果当前页表项无效（alloc传入为0），或不允许分配新的页表页面（物理页分配失败），则返回NULL。
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      // 如果允许分配新的页表页面，则将新分配的页表页面的物理地址写入当前页表项。
      // 初始化新分配的页表页面。
      //alloc参数体现的就是懒加载思想，代码调用过程中传入的alloc值为1，会在遍历到的pte还未建立映射关系时，再申请下一级页表的物理页面，即: 用到时再加载的思想。
      memset(pagetable, 0, PGSIZE);

      // 更新页表项，将物理页表映射到虚拟地址，并标记为有效。
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  // 返回最终页表项的地址，即一级页表中的页表项。
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
/**
 * 根据虚拟地址获取物理地址
 * 
 * pagetable 页表指针，用于映射虚拟地址到物理地址
 * va 虚拟地址，需要转换成物理地址
 * 返回对应的物理地址，如果虚拟地址无效或不受访问权限，则返回0
 * 
 * 该函数首先检查虚拟地址是否超过系统定义的最大虚拟地址范围。
 * 如果超过，则直接返回0。接着，它通过页表和虚拟地址查找相应的页表项。
 * 如果页表项不存在，则返回0。如果页表项存在，但标记为无效（未被映射），则也返回0。
 * 同样，如果页表项存在但标记为不可用户访问，则再次返回0。
 * 最后，如果所有的检查都通过，它将页表项转换为物理地址并返回这个物理地址。
 */
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  // 检查虚拟地址是否超过最大虚拟地址范围
  if(va >= MAXVA)
    return 0;

  // 通过页表和虚拟地址查找页表项
  pte = walk(pagetable, va, 0);
  // 如果页表项不存在，返回0
  if(pte == 0)
    return 0;
  // 如果页表项无效，返回0
  if((*pte & PTE_V) == 0)
    return 0;
  // 如果页表项不可用户访问，返回0
  if((*pte & PTE_U) == 0)
    return 0;
  // 将页表项转换为物理地址
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
uint64
kvmpa(uint64 va)
{
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;
  
  pte = walk(kernel_pagetable, va, 0);
  if(pte == 0)
    panic("kvmpa");
  if((*pte & PTE_V) == 0)
    panic("kvmpa");
  pa = PTE2PA(*pte);
  return pa+off;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
/*
 * 函数：mappages
 * 功能：在给定的页表中映射一段内存页面
 * 参数：
 * - pagetable：页表的指针
 * - va：虚拟地址的起始位置
 * - size：需要映射的内存大小
 * - pa：物理地址的起始位置
 * - perm：页面的访问权限
 * 返回值：成功映射返回0，失败返回-1
 * 
 * 该函数通过遍历给定的虚拟地址范围，为每个页面创建一个新的映射到指定物理地址并设置相应的权限。
 * 如果任何页面已经映射，则会触发panic，表明重复映射。
 */
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  //将我们需要映射的虚拟内存地址范围进行页面对齐操作
  a = PGROUNDDOWN(va); //向下对齐，要分配的虚拟内存起始地址
  last = PGROUNDDOWN(va + size - 1); //向下对齐后，要分配的结束地址
  //对要映射的虚拟地址范围中每个页面建立映射关系
  for(;;){
    //遍历页表得到虚拟地址对应的最后一级页表中的页表项  xv6使用三级页表
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    //如果该页表项已经映射了物理地址，说明已经映射过了，报错
    if(*pte & PTE_V)
      panic("remap");
      //向该页表中具体写入映射信息 ---> 将要映射的物理地址转化为虚拟地址，同时设置页表项的权限等信息
    *pte = PA2PTE(pa) | perm | PTE_V;
    //如果要分配的虚拟地址范围起始和结束重叠，则完成映射，退出循环
    if(a == last)
      break;
    //移动到下一个页面的起始地址和物理地址
    a += PGSIZE;
    //注意物理地址也会增加
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void
// 解除从va开始的映射,va必须是对齐的,映射必须存在,是否释放物理内存是可选的
//释放物理页是可选的，是因为可能存在多个虚拟地址映射到相同物理页的情况
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;
  //传入的虚拟地址需要是对齐后的
  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");
  //遍历虚地址范围,建立映射关系
  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    //遍历定位当前虚拟地址对应的PTE --- 最后一个参数值为0,表示遇到未建立映射的PTE情况下,直接返回0
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    // 是否建立了映射  
    if((*pte & PTE_V) == 0)
      panic("uvmunmap: not mapped");
    // 如果pte并非叶子层的,则说明walk定位返回的有问题
    if(PTE_FLAGS(*pte) == PTE_V)  //取PTE的低10位，即PTE的标志位判断是否有效
      panic("uvmunmap: not a leaf");
    // 是否释放物理内存   
    if(do_free){
      //通过pte得到物理页面起始地址
      uint64 pa = PTE2PA(*pte);
      //释放该物理页
      kfree((void*)pa);
    }
    //清空pte内容
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  // 为新页表分配一个物理页面
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  // 初始化页表  
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
/*
 * uvmalloc函数用于扩大一个进程的虚拟内存空间。
 * 它接受一个页表，以及旧的和新的内存大小作为参数。
 * 如果新的大小小于旧的大小，则返回旧的大小。
 * 否则，它将尝试为新增加的内存空间分配物理内存，并在页表中映射这些新页面。
 * 如果分配和映射过程中出现任何错误，函数将回滚到错误之前的状态，并返回0。
 * 成功完成时，它返回新的内存大小。
 *
 * 参数:
 *   pagetable - 进程的页表
 *   oldsz - 旧的内存大小
 *   newsz - 新的内存大小
 * 返回值:
 *   如果成功，返回新的内存大小；如果失败，返回旧的内存大小或0。
 */
uint64
// 当前进程根页表基地址,旧的堆顶地址,新的堆顶地址
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem; // 用于临时存储分配的物理内存地址
  uint64 a; // 用于遍历新旧内存大小之间的页面的索引

  // 如果新的大小小于旧的大小，无需扩展，直接返回旧的大小
  if(newsz < oldsz)
    return oldsz;

  // 将旧的大小向上取整到页边界，以确保新分配的内存页不会破坏旧的内存布局
  oldsz = PGROUNDUP(oldsz);
  
  // 遍历从旧大小到新大小之间的每个页面
  for(a = oldsz; a < newsz; a += PGSIZE){
    // 从内核内存池中分配一页物理内存
    mem = kalloc();
    // 如果分配失败，即没有剩余的空闲物理页面了，回滚并释放之前已分配的所有资源，然后返回0
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    
    // 清零新分配的内存页，以确保不会泄露任何敏感信息或产生未定义行为
    memset(mem, 0, PGSIZE);
    
    // 将物理内存页映射到进程的虚拟地址空间，并设置适当的页面权限
    // 如果映射失败，回滚并释放之前已分配的所有资源，然后返回0
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  
  // 如果所有操作都成功，返回新的内存大小
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
//uvmdealloc调用uvmunmap(*kernel/vm.c*:174)，uvmunmap使用walk来查找对应的PTE，并使用kfree来释放PTE引用的物理内存。
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  // 如果新的堆顶地址比旧的大,那么直接返回旧的
  if(newsz >= oldsz)
    return oldsz;
   // 确保新的堆顶地址在对齐后比旧的堆顶地址小
  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    // 获取需要释放的解除映射的页面数量
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    // 解除从对齐后的堆顶地址开始的n个页面映射---并释放物理内存
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
//由下至上，递归释放整个多级页表占据的所有物理页
// 叶子层的所有映射关系必须已经都被移除了
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    //满足下面条件,说明还没有递归到叶子层
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  //释放当前页表占据的物理页面
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    //释放旧页表所管理的虚拟地址空间从0到sz内的所有映射，同时释放对应的物理页---释放叶子层的所有映射关系
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);  
  //释放旧页表占据的物理页  
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
  //定位虚地址的pte
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
     //设置pte为u态不可访问
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
// 将数据从内核态 copy 到用户态
int
// 根页表地址,copy到的目标虚地址,数据源地址,copy数据的长度
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    //这个宏的作用是将给定的地址 a 向下舍入到最接近的页面大小 PGSIZE 的较低倍数
    va0 = PGROUNDDOWN(dstva);
    // 得到目标虚地址的物理地址
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    // 向下对齐可能会丢失部分数据,所以这里需要计算   
    n = PGSIZE - (dstva - va0);
    // 该条件成立,说明剩余未copy字节数小于PGSIZE
    if(n > len)
      n = len;
    // 将src源地址处的n字节数据copy到pa0+destva-va0地址开始处  
    memmove((void *)(pa0 + (dstva - va0)), src, n);
    // 剩余待copy字节数
    len -= n;
    // 数据copy起始地址往前推 
    src += n;
    // copy到的目标虚拟地址地址同样前推
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
