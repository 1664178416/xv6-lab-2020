#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"

static int loadseg(pde_t *pgdir, uint64 addr, struct inode *ip, uint offset, uint sz);


// 执行新程序
// path: 程序路径
// argv: 命令行参数数组
// 返回值: 新进程的argc，执行失败返回-1
int
exec(char *path, char **argv)
{
  //用于临时存储字符串
  char *s, *last;
  //用于循环和偏移量计算
  int i, off;
   //sz表示新进程的当前可用内存起始地址,sp指向新进程用户栈栈顶,stackbase代表栈的基地址
  uint64 argc, sz = 0, sp, ustack[MAXARG+1], stackbase;
  // 用于接收elf文件头
  struct elfhdr elf;
   // 用于接收可执行文件对应的inode
  struct inode *ip;
  // 用于接收program header头
  struct proghdr ph;
   // 给子进程的准备的新页表,和子进程的旧页表-->其实也就是copy的父进程的页表
  pagetable_t pagetable = 0, oldpagetable;
   // 获取当前子进程的结构体
  struct proc *p = myproc();

  // 开始一个文件操作
  begin_op();
  // 通过文件名定位其inode
  if((ip = namei(path)) == 0){
    end_op();
    return -1;
  }
    // 确定inode并为当前Inode加锁
  ilock(ip);

  // Check ELF header
  // 从磁盘读取文件的elf头信息
  if(readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
      // 检验可执行文件的魔数是否合法  
  if(elf.magic != ELF_MAGIC)
    goto bad;
   // 为当前子进程分配一个新页表
  if((pagetable = proc_pagetable(p)) == 0)
    goto bad;

  // Load program into memory.
   // 遍历program header数组 -- 依次加载每个segement到内存
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    // 从elf文件中依次读取每个program header
    if(readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    // 只加载类型为LOAD的段--其他用于提供辅助信息的段不进行加载  
    if(ph.type != ELF_PROG_LOAD)
      continue;
    // 段在elf文件中占的大小不能比其在内存中占的大  
    if(ph.memsz < ph.filesz)
      goto bad;
    // 溢出检测  
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    // 从sz地址处开始为每个段分配物理页,并建立与当前段虚地址的映射关系 
    uint64 sz1;
    if((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;
     // 段被加载后,sz可用内存空间指针上移 
    sz = sz1;
    // 如果当前段在程序头中设置的虚地址不对齐,那么也是错误的行为
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    // 加载段的内容到指定的虚拟地址   
    if(loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  // 释放inode锁并结束文件操作
  iunlockput(ip);
  end_op();
  ip = 0;
   // 获取当前子进程结构体
  p = myproc();
  // 子进程旧的内存使用堆顶   
  uint64 oldsz = p->sz;

  // Allocate two pages at the next page boundary.
  // Use the second as the user stack.
  // sz代表新进程的目前可用内存的起始地址 --> segement不断被加载,sz不断上推
  sz = PGROUNDUP(sz);
  uint64 sz1;
  // 在sz地址基础上继续分配两个页面,第二个页面作为用户栈,第一个作为guard page
  if((sz1 = uvmalloc(pagetable, sz, sz + 2*PGSIZE)) == 0)
    goto bad;
  sz = sz1;
  // 设置guard page
  uvmclear(pagetable, sz-2*PGSIZE);
  // sp指向用户栈栈顶---> 栈是向下扩展的
  sp = sz;
  //栈基地址
  stackbase = sp - PGSIZE;

  // Push argument strings, prepare rest of stack in ustack.
  // 将传递给当前程序的参数都推入上面分配的用户栈中
  for(argc = 0; argv[argc]; argc++) {
     // 判断传递的参数个数是否超过了限制
    if(argc >= MAXARG)
      goto bad;
    // 腾出空间  
    sp -= strlen(argv[argc]) + 1;
    // sp指针指向的栈顶地址必须要16字节对齐
    sp -= sp % 16; // riscv sp must be 16-byte aligned
    // 栈溢出
    if(sp < stackbase)
      goto bad;
    // 参数入栈  copyout完成数据从内核态到用户态的拷贝
    //sp栈顶，第一个为页表地址，第二个为用户栈空间栈顶地址，然后用walkaddr和第一个参数找到对应的物理地址，第三个为内核里面要拷贝到用户的数据，配合前面for循环从0拷贝到argc大小的数据，strlen(argv[argc]) + 1是因为字符串末尾有个\0，用来判断结束
    if(copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    // ustack记录每个参数对应的栈中位置    
    ustack[argc] = sp;
  }
  // 标记结束
  ustack[argc] = 0;

  // push the array of argv[] pointers.
  // 将argv指针入栈,此时ustack用于表示argv --> ustack数组被压栈
  sp -= (argc+1) * sizeof(uint64);
  sp -= sp % 16;
  if(sp < stackbase)
    goto bad;
  if(copyout(pagetable, sp, (char *)ustack, (argc+1)*sizeof(uint64)) < 0)
    goto bad;

  // arguments to user main(argc, argv)
  // argc is returned via the system call return
  // value, which goes in a0.
  // a1寄存器作为系统调用参数寄存器,此处保存main函数中需要的第二个参数地址,即argv参数地址
  // 也就是当前栈顶--因为ustack是最后一个被压栈的
  p->trapframe->a1 = sp;

  // Save program name for debugging.
  // 保存程序名,用于debug 
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
   // 将程序名赋值给p->name    
  safestrcpy(p->name, last, sizeof(p->name));
    
  // Commit to the user image.
   // 子进程的旧页表,也就是继承父进程的页表
  oldpagetable = p->pagetable;
  // 子进程的页表指针指向新的页表 
  p->pagetable = pagetable;
  // 更新子进程的内存使用顶部位置
  p->sz = sz;
  // 设置mepc的值为elf的entry,也就是可执行程序的入口地址
  p->trapframe->epc = elf.entry;  // initial program counter = main
  // 设置用户栈栈顶指针
  p->trapframe->sp = sp; // initial stack pointer
  // 释放旧的页表 
  proc_freepagetable(oldpagetable, oldsz);

  //返回传递给当前程序的参数个数,根据系统调用规范,返回值由a0寄存器存放
  return argc; // this ends up in a0, the first argument to main(argc, argv)
// 加载过程中出现错误
 bad:
 // 释放分配给新进程的页表
  if(pagetable)
    proc_freepagetable(pagetable, sz);
    // 释放inode锁
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}

// Load a program segment into pagetable at virtual address va.
// va must be page-aligned
// and the pages from va to va+sz must already be mapped.
// Returns 0 on success, -1 on failure.
static int
// 当前进程根页表,加载段的起始虚地址,对应段数据所在的Inode,段在elf文件中的偏移位置,段长度
loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz)
{
  uint i, n;
  uint64 pa;
  // 虚拟地址必须对齐 
  if((va % PGSIZE) != 0 )
    panic("loadseg: va must be page aligned");
  // 按页读取数据,如果剩余数据不够一页,则全部读取出来
  for(i = 0; i < sz; i += PGSIZE){
     // 通过遍历传入的根页表,返回虚拟地址对应的物理地址 -- 这里的前提是虚拟地址和物理地址直接已经建立了映射关系
    pa = walkaddr(pagetable, va + i);
    if(pa == 0)
      panic("loadseg: address should exist");
      // 如果剩余读取字节数小于PAGE_SIZE，那么本次将剩余字节全部读取出来  
    if(sz - i < PGSIZE)
      n = sz - i;
    else
    //否则每次读取PAGE_SIZE大小的字节数据 
      n = PGSIZE;
      // 从当前传入的Inode中offset+i的偏移位置开始读取n字节的数据到pa地址处
    if(readi(ip, 0, (uint64)pa, offset+i, n) != n)
      return -1;
  }
  
  return 0;
}
