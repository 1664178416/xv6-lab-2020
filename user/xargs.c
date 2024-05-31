#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

/*
让我们以一个具体的例子来解释这段代码的工作方式。假设我们在命令行中输入以下命令：

在这个例子中，./xargs是我们的程序，echo是argv[1]，"one two three"是从标准输入读入的参数。

首先，argv[1]（即echo）被添加到argsbuf数组中。然后，从标准输入读入的参数（即"one two three"）被读入到buf数组中。

当读入的字符是空格或换行符时，一个参数读入完成。例如，当读入' '（空格）时，"one"这个参数读入完成。空格被替换为\0，以分割参数。然后，"one"的地址被添加到argsbuf数组中。

这个过程会重复，直到所有的参数都被读入。最后，argsbuf数组中存储了所有参数的地址，包括"echo"，"one"，"two"和"three"。

然后，run函数被调用，以执行命令。在这个例子中，执行的命令是echo one two three。这个命令会打印出one two three，然后程序结束。
*/


// 带参数列表，执行某个程序
void run(char *program, char **args) {
	if(fork() == 0) { // child exec
		exec(program, args);
		exit(0);
	}
	return; // parent return
}

int main(int argc,char* argv[]){
  if(argc < 2){
    printf("Please enter more parameters\n");
    exit(1);
  }
  else{
    char buf[2048]; // 读入时使用的内存池
	char *p = buf, *last_p = buf; // 当前参数的结束、开始指针
	char *argsbuf[128]; // 全部参数列表，字符串指针数组，包含 argv 传进来的参数和 stdin 读入的参数
	char **args = argsbuf; // 指向 argsbuf 中第一个从 stdin 读入的参数,就是xargs后的参数
	for(int i=1;i<argc;i++) {
		// 将 argv 提供的参数加入到最终的参数列表中
		*args = argv[i];
		args++;
	}
	char **pa = args; // 开始读入参数
	while(read(0, p, 1) != 0) { //全都读到buf里面去
		if(*p == ' ' || *p == '\n') {
			// 读入一个参数完成（以空格分隔，如 `echo hello world`，则 hello 和 world 各为一个参数）
			*p = '\0';	// 将空格替换为 \0 分割开各个参数，这样可以直接使用内存池中的字符串作为参数字符串
						// 而不用额外开辟空间
			*(pa++) = last_p;
			last_p = p+1;

			if(*p == '\n') {
				// 读入一行完成
				*pa = 0; // 参数列表末尾用 null 标识列表结束
				run(argv[1], argsbuf); // 执行最后一行指令
				pa = args; // 重置读入参数指针，准备读入下一行
			}
		}
		p++;
	}
	if(pa != args) { // 如果最后一行不是空行
		// 收尾最后一个参数
		*p = '\0';
		*(pa++) = last_p;
		// 收尾最后一行
		*pa = 0; // 参数列表末尾用 null 标识列表结束
		// 执行最后一行指令
		run(argv[1], argsbuf);
	}
	while(wait(0) != -1) {}; // 循环等待所有子进程完成，每一次 wait(0) 等待一个
	exit(0);
  }
}