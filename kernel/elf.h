// ELF文件格式：

// 在计算机科学中，是一种用于二进制文件、可执行文件、目标代码、共享库和核心转储格式文件。
// ELF是UNIX系统实验室（USL）作为应用程序二进制接口（Application Binary Interface，ABI）而开发和发布的，也是Linux的主要可执行文件格式。
// ELF文件由4部分组成，分别是ELF头（ELF header）、程序头表（Program header table）、节（Section）和节头表（Section header table）。
// 实际上，一个文件中不一定包含全部内容，而且它们的位置也未必如同所示这样安排，只有ELF头的位置是固定的，其余各部分的位置、大小等信息由ELF头中的各项值来决定。


// ELF二进制文件由ELF头：struct elfhdr(*kernel/elf.h*:6)，后面一系列的程序节头（section headers）：struct proghdr(*kernel/elf.h*:25)组成。

// Format of an ELF executable file

#define ELF_MAGIC 0x464C457FU  // "\x7FELF" in little endian

// File header
struct elfhdr {
  //魔数 用于标识文件的类型。对于ELF文件，这个值应该等于ELF_MAGIC。
  uint magic;  // must equal ELF_MAGIC
  uchar elf[12];//存储ELF标识符的其他部分。
  //文件类型例如可执行文件、共享对象文件或者重定位文件等。
  //可重定位文件 ET_REL 1，一般为.o文件
  // 可执行文件 ET_EXEC 2
  // 共享目标文件 ET_DYN 3 一般为.so文件
  ushort type;
  // ELF文件的平台属性例如是针对x86还是ARM等。比如3表示该ELF文件只能在Intel x86机器下使用，这也是我们最常见的情况。
  ushort machine;
  // ELF版本号,一般为常数1
  uint version;
  // 程序执行的入口虚拟地址，可重定位文件一般没有入口地址,对应该值为0
  uint64 entry;
  // program headers在elf文件中的偏移位置  
  uint64 phoff;
  // section headers在elf文件中的偏移位置 
  uint64 shoff;
  // 描述elf文件的属性和特征
  uint flags;
  // elf文件头的大小,以字节为单位
  ushort ehsize;
  // progaram header的大小,以字节为单位
  ushort phentsize;
  // progaram header的数量
  ushort phnum;
  // section header的大小,以字节为单位
  ushort shentsize;
  // section header的数量
  ushort shnum;
  // 包含节名称字符串表的节的索引
  ushort shstrndx;
};

// Program section header
// 段表（Section Header Table）就是保存这些段的基本属性的结构。段表是ELF文件中除了文件头以外最重要的结构，它描述了ELF的各个段的信息，比如每个段的段名、段的长度、在文件中的偏移、读写权限及段的其他属性。

// ELF文件的段结构就是由段表决定的，编译器、链接器和装载器都是依靠段表来定位和访问各个段的属性的。

//描述程序段的重要数据结构
struct proghdr {
  uint32 type; //类型
  uint32 flags; //标志，可读可写可执行
  uint64 off;  //文件偏移
  uint64 vaddr;  //虚拟地址
  uint64 paddr; //物理地址
  uint64 filesz; //文件大小
  uint64 memsz; //内存大小
  uint64 align; //对齐
};

// Values for Proghdr type
//表示程序段的类型是可加载的。在ELF文件格式中，可加载的段是那些需要被加载到内存中以便执行的段。type
#define ELF_PROG_LOAD           1

// Flag bits for Proghdr flags
//表示这个程序段是可执行的。这个标志位是ELF文件格式中的一个标志位，用来描述一个可加载的段是否是可执行的。
#define ELF_PROG_FLAG_EXEC      1
//表示这个程序段是可写的。
#define ELF_PROG_FLAG_WRITE     2
// 表示这个程序段是可读的。
#define ELF_PROG_FLAG_READ      4
