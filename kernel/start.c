#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

void main();
void timerinit();

// entry.S needs one stack per CPU.
__attribute__ ((aligned (16))) char stack0[4096 * NCPU];

// scratch area for timer interrupt, one per CPU.
//存放每个hart对应的时钟中断上下文环境——中断上下文环境占用32*uint64大小
uint64 mscratch0[NCPU * 32];

// assembly code in kernelvec.S for machine-mode timer interrupt.
extern void timervec();

// entry.S jumps here in machine mode on stack0.
void
start()
{
  // set M Previous Privilege mode to Supervisor, for mret.
  // 设置mstatus的MPP位为Supervisor态，这个态是操作系统的最高特权级别
  unsigned long x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK;
  x |= MSTATUS_MPP_S;
  w_mstatus(x);

  // set M Exception Program Counter to main, for mret.
  // requires gcc -mcmodel=medany
  //设置mepc寄存器指向main函数地址
  w_mepc((uint64)main);

  // disable paging for now.
  //启动阶段禁用分页功能，意味着取消了地址翻译和保护机制，所有内存访问将直接使用物理地址而不经过虚拟地址到物理地址的映射
  w_satp(0);

  // delegate all interrupts and exceptions to supervisor mode.
  //将所有中断和异常委托交给Supervisor模式处理
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  //设置SIE寄存器相关位，从而开启S态下的外部中断，时钟中断和软件中断
  //外部中断：SIE_SEIE，有关外部中断的位，包括串口中断、时钟中断、外部设备中断等
  //时钟中断：SIE_STIE，时钟中断使能位，开启时钟中断，时钟中断是由CLINT产生的，CLINT是一个计时器，用于产生时钟中断
  //软件中断：SIE_SSIE，软件中断使能位，开启软件中断，软件中断是由软件产生的，通过调用ecall指令产生
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);

  // ask for clock interrupts.
  //初始化硬件定时器模块
  timerinit();

  // keep each CPU's hartid in its tp register, for cpuid().
  //获取当前的hart id，即当前CPU的id
  int id = r_mhartid();
  //在Machine模式下，tp寄存器存储当前硬件线程的唯一标识符（hartid），可用于识别不同的处理器核心
  //Machine模式是RISC-V的最高特权级别，另外还有Supervisor模式和User模式。
  //Machine模式是操作系统的最高特权级别，用于操作系统的内核代码运行，Supervisor模式是操作系统的次高特权级别，用于操作系统的内核代码运行，User模式是操作系统的最低特权级别，用于用户程序运行。
  w_tp(id);

  // switch to supervisor mode and jump to main().
  //执行汇编指令：mret，这个指令会将处理器从Machine模式切换到Supervisor模式，并跳转到mepc寄存器中的地址，即main函数的地址
  asm volatile("mret");
}

// set up to receive timer interrupts in machine mode,
// which arrive at timervec in kernelvec.S,
// which turns them into software interrupts for
// devintr() in trap.c.
void
timerinit()
{
  // each CPU has a separate source of timer interrupts.
  int id = r_mhartid();

  // ask the CLINT for a timer interrupt.
   // 设置时钟中断间隔大约为1毫秒发生一次 --> 硬件1000000次tick大约为1毫秒(qemu模拟出来的)
  int interval = 1000000; // cycles; about 1/10th second in qemu.
  //初始化MTIMECMP寄存器的值 = MTIME+1毫秒间隔 ---> 设置下一次时钟中断发生在一毫秒后
  *(uint64*)CLINT_MTIMECMP(id) = *(uint64*)CLINT_MTIME + interval;

  // prepare information in scratch[] for timervec.
  // scratch[0..3] : space for timervec to save registers.
  // scratch[4] : address of CLINT MTIMECMP register.
  // scratch[5] : desired interval (in cycles) between timer interrupts.
   //mscratch0数组存放所有hart的时钟中断上下环境
  uint64 *scratch = &mscratch0[32 * id];
  //获取当前hart对应的MTIMECMP寄存器的地址
  scratch[4] = CLINT_MTIMECMP(id);
  //存放当前hart时钟中断对应的间隔
  scratch[5] = interval;
  //当前hart的mscratch寄存器指向hart的scratch区域，该区域存放当前hart时钟中断上下文环境
  w_mscratch((uint64)scratch);

  // set the machine-mode trap handler.
  //设置mvetc指向时钟中断处理函数地址
  w_mtvec((uint64)timervec);

  // enable machine-mode interrupts.
  //开启M态的全局中断
  w_mstatus(r_mstatus() | MSTATUS_MIE);

  // enable machine-mode timer interrupts.
  //开启M态的时钟中断
  w_mie(r_mie() | MIE_MTIE);
}
