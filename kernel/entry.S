	# qemu -kernel loads the kernel at 0x80000000
        # and causes each CPU to jump there.
        # kernel.ld causes the following code to
        # be placed at 0x80000000.
        # 链接器脚本中哦哦已经将代码段中的加载地址设置为0x80000000
        # 链接器脚本中定义了.text段，将代码段中的加载地址设置为0x80000000
.section .text
_entry:
	# set up a stack for C.
        # stack0 is declared in start.c,
        # with a 4096-byte stack per CPU.
        #为内核c代码执行申请一块stack栈空间，stack0这个栈是定义在start.c文件中，并且每个cpu分配一个4096字节的栈空间
        # sp = stack0 + (hartid * 4096)
        # 确保每个hart的栈空间隔离，且是连续的 （ hart的意思是多核处理器 ）
        la sp, stack0
        li a0, 1024*4
	csrr a1, mhartid
        addi a1, a1, 1
        mul a0, a0, a1
        add sp, sp, a0

        #上面这段是汇编代码，将sp指向stack0 的符号地址
        # a0 = 1024 * 4 = 4096
        # a1 = 当前hartid
        # a1++
        # a0 = a1*a0
        # sp += a0
	# jump to start() in start.c,跳转到start.c文件的start函数处执行
        call start
spin:
        j spin
