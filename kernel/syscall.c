#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

// Fetch the uint64 at addr from the current process.
int
fetchaddr(uint64 addr, uint64 *ip)
{
  struct proc *p = myproc();
  if(addr >= p->sz || addr+sizeof(uint64) > p->sz)
    return -1;
  if(copyin(p->pagetable, (char *)ip, addr, sizeof(*ip)) != 0)
    return -1;
  return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Returns length of string, not including nul, or -1 for error.
int
fetchstr(uint64 addr, char *buf, int max)
{
  struct proc *p = myproc();
  int err = copyinstr(p->pagetable, buf, addr, max);
  if(err < 0)
    return err;
  return strlen(buf);
}

/**
 * 获取进程的原始参数值
 * 
 * 该函数用于获取当前进程的第n个参数的原始值。这些参数在进程调用系统调用或中断处理程序时保存在陷阱帧（Trap Frame）中。
 * 参数n指示要获取的参数的位置，从0开始计数。
 * 
 * @param n 参数的位置，取值范围为0到5。
 * @return 返回第n个参数的原始值。
 * 
 * 注意：如果n超出有效范围（0到5），函数将触发panic，因为这表示代码存在错误，需要立即停止执行。
 */
static uint64
argraw(int n)
{
  // 获取当前进程的结构体指针
  struct proc *p = myproc();
  
  // 根据参数n的位置，返回对应参数的原始值
  switch (n) {
  case 0:
    return p->trapframe->a0;
  case 1:
    return p->trapframe->a1;
  case 2:
    return p->trapframe->a2;
  case 3:
    return p->trapframe->a3;
  case 4:
    return p->trapframe->a4;
  case 5:
    return p->trapframe->a5;
  }
  
  // 如果n超出预期范围，表示存在编程错误，触发panic
  panic("argraw");
  return -1; // 永远不会到达这里，因为panic会中断执行
}

// Fetch the nth 32-bit system call argument.
/**
 * 函数argint的目的是从命令行参数中解析出一个整数。
 * 
 * @param n 指定要解析的参数的索引。
 * @param ip 一个指针，函数将解析出的整数存储在这个指针所指向的位置。
 * 
 * @return 函数始终返回0，表示解析操作成功或不适用。
 */
int
argint(int n, int *ip)
{
  /* 调用argraw函数来获取命令行参数n对应的原始值，并通过ip指针将这个值存储到内存中 */
  *ip = argraw(n);
  /* 解析成功，返回0 */
  return 0;
}

// Retrieve an argument as a pointer.
// Doesn't check for legality, since
// copyin/copyout will do that.
int
argaddr(int n, uint64 *ip)
{
  *ip = argraw(n);
  return 0;
}

// Fetch the nth word-sized system call argument as a null-terminated string.
// Copies into buf, at most max.
// Returns string length if OK (including nul), -1 if error.
int
argstr(int n, char *buf, int max)
{
  uint64 addr;
  if(argaddr(n, &addr) < 0)
    return -1;
  return fetchstr(addr, buf, max);
}

extern uint64 sys_chdir(void);
extern uint64 sys_close(void);
extern uint64 sys_dup(void);
extern uint64 sys_exec(void);
extern uint64 sys_exit(void);
extern uint64 sys_fork(void);
extern uint64 sys_fstat(void);
extern uint64 sys_getpid(void);
extern uint64 sys_kill(void);
extern uint64 sys_link(void);
extern uint64 sys_mkdir(void);
extern uint64 sys_mknod(void);
extern uint64 sys_open(void);
extern uint64 sys_pipe(void);
extern uint64 sys_read(void);
extern uint64 sys_sbrk(void);
extern uint64 sys_sleep(void);
extern uint64 sys_unlink(void);
extern uint64 sys_wait(void);
extern uint64 sys_write(void);
extern uint64 sys_uptime(void);
extern uint64 sys_sigalarm(void);  //lab4-3
extern uint64 sys_sigreturn(void);  //lab4-3

static uint64 (*syscalls[])(void) = {
[SYS_fork]    sys_fork,
[SYS_exit]    sys_exit,
[SYS_wait]    sys_wait,
[SYS_pipe]    sys_pipe,
[SYS_read]    sys_read,
[SYS_kill]    sys_kill,
[SYS_exec]    sys_exec,
[SYS_fstat]   sys_fstat,
[SYS_chdir]   sys_chdir,
[SYS_dup]     sys_dup,
[SYS_getpid]  sys_getpid,
[SYS_sbrk]    sys_sbrk,
[SYS_sleep]   sys_sleep,
[SYS_uptime]  sys_uptime,
[SYS_open]    sys_open,
[SYS_write]   sys_write,
[SYS_mknod]   sys_mknod,
[SYS_unlink]  sys_unlink,
[SYS_link]    sys_link,
[SYS_mkdir]   sys_mkdir,
[SYS_close]   sys_close,
[SYS_sigalarm] sys_sigalarm,  //lab4-3
[SYS_sigreturn] sys_sigreturn,  //lab4-3
};

void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  num = p->trapframe->a7;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    p->trapframe->a0 = syscalls[num]();
  } else {
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
