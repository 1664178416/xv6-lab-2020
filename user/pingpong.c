#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
int main(int argc,char* argv[]){ //argc表示参数个数，argv表示参数数组
    int p1[2],p2[2]; //创建一个管道
    pipe(p1);
    pipe(p2);
    //创建管道，得到一个长度为2的数组
    //p1[0]表示读端，p1[1]表示写端
    int pid = fork(); //创建一个子进程
    if(pid == 0){ //子进程
      char buf;
      read(p1[0],&buf,1); //子进程读取一个字符
      printf("%d: received ping\n",getpid());
      write(p2[1],&buf,1); //子进程写入一个字符
    }else{ //父进程
      write(p1[1],"1",1); //父进程写入一个字符
      char buf;
      read(p2[0],&buf,1); //父进程读取一个字符
      printf("%d: received pong\n",getpid());
      wait(0); //等待子进程结束
    }
    exit(0);
}