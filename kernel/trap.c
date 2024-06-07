#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//usertrap的任务是确定陷阱的原因，处理并返回(kernel/trap.c:37)。
void
usertrap(void)
{
  int which_dev = 0;
  // 确保是用户态发生的trap  
  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  // 由于我们现在处于内核态,所以后续trap发生都交给kernelvec处理
  w_stvec((uint64)kernelvec);
  
  struct proc *p = myproc();
  
  // save user program counter.
  // 将spec保存到trapframe中
  p->trapframe->epc = r_sepc();
  // 错误码为8,表示产生的是系统调用异常
  if(r_scause() == 8){
    // system call
    // 如果当前进程被杀死了,然后销毁当前进程
    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    // 产生异常时,spec保存的是发生异常的那条指令地址,这里为了防止产生无限循环,将trapframe中保存的epc值+4
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    // 发生trap时,硬件会自动关闭全局中断,我们这里确保保存好了上面这些寄存器内容后,重新打开中断
    intr_on();
    // 系统调用号保存在a7寄存器中,调用syscall函数,根据系统调用号进行系统调用的派发
    syscall();
  }
  // 处理设备中断--判断是什么设备中断
  else if((which_dev = devintr()) != 0){
    // ok
  } else {
    //无法识别的cause错误码
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }
  
  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  // 如果是时钟中断,则主动让出当前cpu
  if(which_dev == 2)
    yield();
  // 如果不是时钟中断,说明是系统调用执行完毕后,执行到这里,则进行trap返回流程
  //该函数已经分析过了
  usertrapret();
}

//
// return to user space
//usertrapret函数中会执行S态返回用户态的操作:
void
usertrapret(void)
{
  // 获取当前进程
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  //设置sstatus的值为0,以此来关闭S态下全局中断
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  // stvec更改为指向uservec
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  //保存内核根页表的位置
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  //保存当前进程的内核栈栈顶指针
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  // 用户态发生trap时会调用usertrap进行处理
  p->trapframe->kernel_trap = (uint64)usertrap;
  //保存hart id
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  //设置status的SPP位为0,这样sret指令执行后,会恢复到user特权下
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  //设置status的SPIE位为1,这样sret指令执行后, 会重新打开全局中断 
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  //重新设置sstatus寄存器的值
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  //设置sepc指向trapframe中保存的epc,也就是我们先前设置好的程序的入口地址
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  // 设置stap在sret执行后,指向进程的用户态根页表
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  // fn为userret函数的入口地址
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  // TRAPFRAME帧用于在用户态和内核态切换时进行上下文的保存和恢复
  // 该帧在proc_pagetable初始化进程的用户态页表时分配物理页并在用户态页表建立映射
  // 其位置就在TRAMPOLINE帧下面
  // userret函数传入当前用户态上下文trapframe地址,和用户态的根页表
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

