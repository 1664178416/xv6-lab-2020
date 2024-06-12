//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  if(argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
//xv6硬链接实现方式
uint64 sys_link(void) {
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  // 从用户态获取参数 old 和 new，分别表示旧路径和新路径
  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  // 开始文件系统操作事务
  begin_op();

  // 根据旧路径名找到 inode
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  // 对 inode 进行加锁
  ilock(ip);

  // 检查旧路径对应的 inode 是否为目录，如果是目录则不能创建硬链接
  if(ip->type == T_DIR){
    iunlockput(ip); // 解锁 inode 并释放引用
    end_op();
    return -1;
  }

  // 增加 inode 的链接数，因为即将创建一个新的硬链接指向该 inode
  ip->nlink++;
  iupdate(ip); // 更新 inode 的磁盘信息
  iunlock(ip); // 解锁 inode

  // 解析新路径中的目录名和文件名
  if((dp = nameiparent(new, name)) == 0)
    goto bad;

  // 对目录的 inode 进行加锁
  ilock(dp);

  // 检查新路径所在的目录的设备号与旧路径所在的设备号是否相同
  // 如果不相同，或者在新路径所在的目录中无法创建新的目录项，说明创建硬链接失败
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp); // 解锁目录的 inode 并释放引用
    goto bad;
  }

  // 解锁目录的 inode 并释放引用
  iunlockput(dp);

  // 释放旧路径对应的 inode 的引用
  iput(ip);

  // 结束文件系统操作事务
  end_op();

  return 0;

bad:
  // 创建硬链接失败时，需要回滚之前对旧路径 inode 的修改，即减少链接数
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}


// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64 sys_unlink(void) {
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  // 从用户态获取参数 path，表示要删除的文件路径
  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  // 开始文件系统操作事务
  begin_op();

  // 根据提供的路径，找到文件的父目录
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  // 对父目录的 inode 进行加锁
  ilock(dp);

  // 不能删除 "." 或 ".." 目录项
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  // 根据文件名在父目录中查找对应的目录项
  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;

  // 对文件的 inode 进行加锁
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");

  // 如果要删除的是目录，需要确保目录为空
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip); // 解锁 inode 并释放引用
    goto bad;
  }

  // 清空目录项，并写回父目录
  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");

  // 如果删除的是目录，需要更新父目录的链接数
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp); // 解锁父目录并释放引用

  // 更新文件的链接数，并解锁文件的 inode 并释放引用
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  // 结束文件系统操作事务
  end_op();

  return 0;

bad:
  // 删除操作失败时，需要解锁父目录并释放引用，并结束文件系统操作事务
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

/* 系统调用open的实现 */
uint64
sys_open(void)
{
  /* 路径名缓冲区 */
  char path[MAXPATH];
  /* 文件描述符和打开模式 */
  int fd, omode;
  /* 文件结构指针 */
  struct file *f;
  /* i节点指针 */
  struct inode *ip;
  /* 临时变量，用于存储函数调用返回值 */
  int n;

  /* 获取路径名和打开模式，如果出错则返回-1 */
  if((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
    return -1;

  /* 开始一个文件操作 */
  begin_op();

  /* 如果需要创建文件 */
  if(omode & O_CREATE){
    /* 调用create函数创建文件，如果失败则返回-1 */
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    int symlink_depth = 0;
    /* 查找已存在的文件，如果失败则返回-1 */
    while(1){
      if((ip = namei(path)) == 0){
        end_op();
        return -1;
      }
      /* 加锁i节点 */
      ilock(ip);

      if(ip->type == T_SYMLINK && (omode & O_NOFOLLOW) == 0){
        //在跟踪符号链接时需要额外考虑到符号链接的目标可能还是符号链接, 此时需要递归的去跟踪目标链接直至得到真正的文件. 而这其中需要解决两个问题: 一是符号链接可能成环, 这样会一直递归地跟踪下去, 因此需要进行成环的检测; 另一方面是需要对链接的深度进行限制, 以减轻系统负担,这里把最大深度设成10
        if(++symlink_depth > 10){
          // too many layer of symlinks, might be a loop
          iunlockput(ip);
          end_op();
          return -1;
        }
        if(readi(ip, 0, (uint64)path, 0, MAXPATH) < 0){
          iunlockput(ip);
          end_op();
          return -1;
        }
        iunlockput(ip);
        
      }else{
        break;
      }
    }

    /* 如果是目录文件且不是只读打开，返回-1 */
    if(ip->type == T_DIR && omode != O_RDONLY){
      // // 如果是目录，并且打开模式不是 O_RDONLY（只读），则不能打开
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  /* 如果是设备文件，检查设备号是否有效，否则返回-1 */
  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  /* 分配文件结构，如果失败则返回-1 */
  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  // //处理符号链接，即软连接,如果
  // if(ip->type == T_SYMLINK && !(omode & O_NOFOLLOW)){ //如果不是不可追踪
  //   //若符号链接指向的仍旧是符号链接，则递归跟随它，直到找到真正的指向文件
  //   //深度不能超过MAX_SYMLINK_DEPTH
  //   for(int i = 0; i < MAX_SYMLINK_DEPTH; i++){
  //     //读出符号链接指的路径
  //   }
  // }

  /* 根据文件类型设置文件结构的类型和主要设备号 */
  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  /* 设置文件结构的i节点指针和读写权限 */
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  /* 如果要求截断文件，且文件类型是普通文件，则截断文件 */
  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  /* 解锁i节点 */
  iunlock(ip);
  /* 结束文件操作 */
  end_op();

  /* 返回文件描述符 */
  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  if((argstr(0, path, MAXPATH)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if(argaddr(0, &fdarray) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

//实现符号软链接
uint64 sys_symlink(void){
  char target[MAXPATH],path[MAXPATH];
  struct  inode *ip_path;
  //从用户态拿到参数target，path，分别表示软连接的目标路径和要软连接的路径
  if(argstr(0, target, MAXPATH) < 0 || argstr(1, path, MAXPATH) < 0){
    return -1;
  } 
  //开始文件系统操作食物
  begin_op();
  //创建一个新的inode，类型为T_SYSLINK，表示软链接
  //create函数返回锁定的inode
  ip_path = create(path,T_SYMLINK,0,0);
  if(ip_path == 0){
    end_op();
    return -1;
  }

  //向inode数据块写入目标路径（target），把target写到ip_path中，即将软链接的目标路径写入inode的数据块
  if(writei(ip_path,0,(uint64)target,0,MAXPATH) < MAXPATH){
    iunlockput(ip_path); //解锁inode并释放引用
    end_op(); //结束文件系统操作事务
    return -1;
  }

  iunlockput(ip_path);//解锁inode并释放引用
  end_op();
  return 0;

}