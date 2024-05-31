#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"


void find(char* path,char* target){
  char buf[512],*p;
  int fd;
  struct dirent de;
  struct stat st;
  if((fd = open(path, 0)) < 0){
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }
  switch(st.type){
	case T_FILE:
		// 如果是文件，直接比较路径是否相同，若相同则输出路径
		if(strcmp(path+strlen(path)-strlen(target), target) == 0) {
			printf("%s\n", path);
		}
		break;
  case T_DIR:
		if(strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)){
			printf("find: path too long\n");
			break;
		}
		strcpy(buf, path);
		p = buf+strlen(buf);
		*p++ = '/';
		while(read(fd, &de, sizeof(de)) == sizeof(de)){
			if(de.inum == 0)
				continue;
			memmove(p, de.name, DIRSIZ);  //复制之后p的指向并不会变，p只是起一个指针作用，指向path+一个'/'后的末尾
			p[DIRSIZ] = 0;
			if(stat(buf, &st) < 0){
				printf("find: cannot stat %s\n", buf);
				continue;
			}  //到这里其实都是ls.c的内容,重点是接下来要递归找
      //不进入 '.' 和 '..' 目录
      if(strcmp(buf+strlen(buf)-2, "/.") != 0 && strcmp(buf+strlen(buf)-3, "/..") != 0) {
				find(buf, target); // 在while循环里面递归查找，相当于path里面的每个都递归一遍(已经把里面的文件填进去路径了 也有/)
			}
		}
		break;
	}
	close(fd);
}

/*
 * 主函数：检查命令行参数数量，构造目标路径，并调用find函数进行查找。
 * argc：命令行参数的数量。
 * argv：命令行参数的值，是一个字符数组。
 * 返回值：程序正常运行时不返回，若参数不足则直接退出。
 */
int main(int argc,char* argv[]){
  // 检查命令行参数是否少于3个，若是则直接退出程序
  if(argc < 3){
    exit(0);
  }
  char target[DIRSIZ+1]; // 定义目标路径字符串，长度为DIRSIZ+1，保证最后一个字符为'\0'
  target[0] = '/'; // 为路径添加根目录符号'/'
  strcpy(target+1,argv[2]); // 将命令行参数中的路径添加到target字符串中
  find(argv[1],target); // 调用find函数，开始查找
  //argv[0]是自己
  exit(0);
}