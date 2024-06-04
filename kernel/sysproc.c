#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

//sbrk是一个用于进程减少或增长其内存的系统调用:
// sys_sbrk系统调用通过C中的一个包装函数brk来访问
// 该函数接受一个参数，指定要增加或减少程序数据段的内存量。作为返回值，它提供一个指向新分配内存起始位置的指针  
uint64
sys_sbrk(void)
{
  int addr;
  int n;
  //从a0系统调用参数寄存器中取出参数值 
  if(argint(0, &n) < 0)
    return -1;
  // 返回一个指向新分配内存起始位置的指针 -- 当前进程堆顶位置
  addr = myproc()->sz;
  // 增加内存
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64 
sys_trace(void){
  //获取系统调用参数
  argint(0,&(myproc()->trace_mask));
  return 0;
}

uint64
sys_sysinfo(void){
  struct sysinfo info;
  freebytes(&info.freemem);
  procnum(&info.nproc);

  //a0寄存器作为系统调用的参数寄存器,从中取出存放 sysinfo 结构的用户态缓冲区指针
  uint64 dstaddr;
  argaddr(0,&dstaddr);

  //使用copyout，结合当前进程页表，获得传进来的指针（逻辑地址）对应的物理地址
  //然后将  &info中的数据复制到该指针所指的位置，供用户进程使用
  if(copyout(myproc()->pagetable,dstaddr,(char*)&info,sizeof(info))<0)return -1;
  return 0;
}