#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

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

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
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
  backtrace();
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


//lab4-3
uint64 sys_sigreturn(void){
  if(myproc()->trapframecopy != myproc()->trapframe + 512)return -1;
  memmove(myproc()->trapframe,myproc()->trapframecopy,sizeof(struct trapframe));
  //myproc()->is_alarming = 0;
  myproc()->trapframecopy = 0;
  myproc()->passedticks = 0;
  return myproc()->trapframe->a0;	// 返回a0,避免被返回值覆盖
  //return 0;,好像return 0也行
}

//lab4-3
uint64 sys_sigalarm(void){
  int interval;
  uint64 handler;
  struct proc *p = myproc();
  // 要求时间间隔非负
  if (argint(0, &interval) < 0 || argaddr(1, &handler) < 0 || interval < 0) {
      return -1;
  }
  p->interval = interval;
  p->handler = handler;
  p->passedticks = 0;    // 重置过去时钟数

  return 0;
}