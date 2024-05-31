#include<kernel/types.h>
#include<kernel/stat.h>
#include<user/user.h>

void dfs(int pleft[2]){
  int p;
  read(pleft[0],&p,sizeof(p));
  if(p == -1){
    close(pleft[0]);
    exit(0);
  }
  printf("prime %d\n",p);
  int pright[2];
  pipe(pright);
  if(fork() == 0){ //如果是下一次递归，那么pleft不需要读，pright不需要写
    close(pleft[0]);
    close(pright[1]);
    dfs(pright);
  }else{ //如果是父进程，那么pright不需要写，pleft的读还是得保留，因为上面还在读
    close(pright[0]);
    int q;
    while(read(pleft[0],&q,sizeof(q)) && q != -1){
      if(q % p != 0){ //只有过了本轮筛选才能进入下一轮
        write(pright[1],&q,sizeof(q));
      }
    }
    write(pright[1],&q,sizeof(q)); //-1作为读完的标志；
    wait(0);
    exit(0);
  }

}

int main(int argc,char* argv[]){
  int p[2];
  pipe(p);
  if(fork() == 0){ //子进程，不需要用到写端，只需要读
    close(p[1]);
    dfs(p);
  }
  else{ //父进程，不需要读端
    close(p[0]);
    int i;
    for(i = 2;i <= 35;i++){
      write(p[1],&i,sizeof(i));
    }
    i = -1;
    write(p[1],&i,sizeof(i)); //作为读完的标志；
    
  }
  wait(0); //等待第一个递归完成；
  exit(0);
}