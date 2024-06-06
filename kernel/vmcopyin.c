#include "param.h"
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"

//
// This file contains copyin_new() and copyinstr_new(), the
// replacements for copyin and coyinstr in vm.c.
//

static struct stats {
  int ncopyin;
  int ncopyinstr;
} stats;

int
statscopyin(char *buf, int sz) {
  int n;
  n = snprintf(buf, sz, "copyin: %d\n", stats.ncopyin);
  n += snprintf(buf+n, sz, "copyinstr: %d\n", stats.ncopyinstr);
  return n;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
/**
 * 将进程地址空间中的源地址内容复制到用户空间的目标地址。
 * 
 * @param pagetable 进程的页表，用于地址转换。
 * @param dst 目标地址，即用户空间中的内存地址，数据将被复制到这里。
 * @param srcva 源虚拟地址，即进程地址空间中的内存地址，数据将从这里复制。
 * @param len 要复制的数据长度。
 * 
 * @return 如果复制成功，返回0；如果复制过程中出现错误，如源地址无效或超出进程地址空间，返回-1。
 * 
 * 该函数主要用于从进程的地址空间中复制数据到用户空间。它首先检查源地址和长度是否合法，
 * 然后使用memmove函数进行实际的数据复制操作。统计信息ncopyin用于记录复制操作的次数。
 */
int  
copyin_new(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  // 获取当前进程的信息
  struct proc *p = myproc();

  // 检查源地址是否在当前进程的地址空间内，以及复制的长度是否超出地址空间
  if (srcva >= p->sz || srcva+len >= p->sz || srcva+len < srcva)
    return -1; // 如果检查失败，返回错误代码-1

  // 实际执行数据复制操作，将源地址的内容复制到目标地址
  memmove((void *) dst, (void *)srcva, len);

  // 更新统计信息，记录一次copyin操作
  stats.ncopyin++;   // XXX lock

  return 0; // 如果复制成功，返回0
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr_new(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  struct proc *p = myproc();
  char *s = (char *) srcva;
  
  stats.ncopyinstr++;   // XXX lock
  for(int i = 0; i < max && srcva + i < p->sz; i++){
    dst[i] = s[i];
    if(s[i] == '\0')
      return 0;
  }
  return -1;
}
