// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13
#define HASH(id) (id % NBUCKET)

struct hashbuf {
  struct buf head; //头节点
  struct spinlock lock; //锁  
};

struct {
  // struct spinlock lock;
  // struct buf buf[NBUF];

  // // Linked list of all buffers, through prev/next.
  // // Sorted by how recently the buffer was used.
  // // head.next is most recent, head.prev is least.
  // struct buf head;
  //放弃使用链表而采用散列桶
  struct buf buf[NBUF];
  struct hashbuf buckets[NBUCKET]; //散列桶
} bcache;

void
binit(void)
{
  struct buf *b;

  // initlock(&bcache.lock, "bcache");

  // // Create linked list of buffers
  // bcache.head.prev = &bcache.head;
  // bcache.head.next = &bcache.head;
  // for(b = bcache.buf; b < bcache.buf+NBUF; b++){
  //   b->next = bcache.head.next;
  //   b->prev = &bcache.head;
  //   initsleeplock(&b->lock, "buffer");
  //   bcache.head.next->prev = b;
  //   bcache.head.next = b;
  // }
  char lockname[16];
  for(int i = 0; i < NBUCKET; i++){
    //初始化散列表的自旋锁
    snprintf(lockname,sizeof(lockname),"bcache_%d",i);
    initlock(&bcache.buckets[i].lock,lockname);

    // 初始化散列桶的头节点，双向链表，有点力扣LRU题的那味了。。。
    bcache.buckets[i].head.prev = &bcache.buckets[i].head;
    bcache.buckets[i].head.next = &bcache.buckets[i].head;
  }

  for(b = bcache.buf; b < bcache.buf+NBUF;b++){
    //使用头插法初始化缓冲区列表，全部放到散列桶0上
    b->next = bcache.buckets[0].head.next;
    b->prev = &bcache.buckets[0].head;
    initsleeplock(&b->lock, "buffer");
    bcache.buckets[0].head.next->prev = b;
    bcache.buckets[0].head.next = b;
  }
  
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
//当没有找到指定的缓冲区时进行分配，分配方式是优先从当前列表遍历，找到一个没有引用且timestamp最小的缓冲区，如果没有就申请下一个桶的锁，并遍历该桶，找到后将该缓冲区从原来的桶移动到当前桶中，最多将所有桶都遍历完。在代码中要注意锁的释放和获取，不要造成死锁。
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int bid = HASH(blockno);
  acquire(&bcache.buckets[bid].lock);
  //在当前桶里面查找,此处是双向链表，首尾相连，所以不需要判断是否为空，直接判断是否回来了
  for(b = bcache.buckets[bid].head.next; b != &bcache.buckets[bid].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;

      //记录使用的时间戳
      acquire(&tickslock);
      b->timestamp = ticks;
      release(&tickslock);

      release(&bcache.buckets[bid].lock);
      //这个函数不仅获取锁，而且在等待锁的过程中会使线程进入睡眠状态。这意味着线程不会占用CPU资源进行忙等待，而是会被挂起，直到它可以获得锁。当锁变得可用时，线程会被唤醒继续执行。这种机制通常用于减少CPU的无效利用，特别是在锁被持有时间较长的情况下。普通的锁会阻塞
      acquiresleep(&b->lock); 
      return b;
    }
  }

    //若是当前buf还没有缓存
    b = 0;
    struct buf* tmp;
    //从当前散列桶开始查找
    for(int i = bid,cycle = 0; cycle != NBUCKET; i = (i+1)%NBUCKET){
      cycle++;
      //如果遍历刀当前桶，就不需要获取锁了
      if(i!=bid){
        //bget中重新分配需要持有两个锁，如果桶a持有自己的锁，再申请桶b的锁，与此同时如果桶b持有自己的锁，再申请桶a的锁就会造成死锁！因此代码中使用了if(!holding(&bcache.bucket[i].lock))来进行检查。此外，代码优先从自己的桶中获取缓冲区，如果没有再去其他桶中获取，这样可以减少锁的竞争。
        if(!holding(&bcache.buckets[i].lock))
          acquire(&bcache.buckets[i].lock);
        else
          continue;
      }
      for(tmp = bcache.buckets[i].head.next; tmp != &bcache.buckets[i].head;tmp = tmp->next){
        if(tmp->refcnt == 0 && (b == 0 || tmp->timestamp < b->timestamp)){
          b = tmp; //如果该tmp没被用同时时间戳比b小，那就把b扔了（或者如果直接b是为0的话就直接拿tmp当作b，反正就是找到没被引用且timestamp最小的
        }
      }
      if(b){
        //如果是从其他散列桶中拿到的，那么就用头插法插入到当前桶
        if(i != bid){
          //先把b从之前位置取出来
          b->next->prev = b->prev;
          b->prev->next = b->next;
          release(&bcache.buckets[i].lock);
          //插入到当前桶
          b->next = bcache.buckets[bid].head.next;
          b->prev = &bcache.buckets[bid].head;
          bcache.buckets[bid].head.next->prev = b;
          bcache.buckets[bid].head.next = b;
        }
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0; //标记为无效,即没使用过
        b->refcnt = 1;

        //更新时间戳
        acquire(&tickslock);
        b->timestamp = ticks;
        release(&tickslock);
        
        release(&bcache.buckets[bid].lock);
        acquiresleep(&b->lock);
        return b;
      }else{
        //在当前散列通中没找到，直接释放锁，然后找下一个桶
        if(i != bid)
          release(&bcache.buckets[i].lock);
      }
    }
    panic("bget: no buffers");
  // // Is the block already cached?
  // for(b = bcache.head.next; b != &bcache.head; b = b->next){
  //   if(b->dev == dev && b->blockno == blockno){
  //     b->refcnt++;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }

  // // Not cached.
  // // Recycle the least recently used (LRU) unused buffer.
  // for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
  //   if(b->refcnt == 0) {
  //     b->dev = dev;
  //     b->blockno = blockno;
  //     b->valid = 0;
  //     b->refcnt = 1;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }
  // panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  // releasesleep(&b->lock);

  // acquire(&bcache.lock);
  // b->refcnt--;
  // if (b->refcnt == 0) {
  //   // no one is waiting for it.
  //   b->next->prev = b->prev;
  //   b->prev->next = b->next;
  //   b->next = bcache.head.next;
  //   b->prev = &bcache.head;
  //   bcache.head.next->prev = b;
  //   bcache.head.next = b;
  // }
  
  // release(&bcache.lock);
  //不在获取全局锁


  int bid = HASH(b->blockno);
  releasesleep(&b->lock);
  acquire(&bcache.buckets[bid].lock);
  b->refcnt--;
  //更新时间戳
  //由于LRU改为时间戳判定，不需要再使用头插法
  acquire(&tickslock);
  b->timestamp = ticks;
  release(&tickslock);
  release(&bcache.buckets[bid].lock);
}

void
bpin(struct buf *b) {
  int bid = HASH(b->blockno);
  acquire(&bcache.buckets[bid].lock);
  b->refcnt++;
  release(&bcache.buckets[bid].lock);
}

void
bunpin(struct buf *b) {
  int bid = HASH(b->blockno);
  acquire(&bcache.buckets[bid].lock);
  b->refcnt--;
  release(&bcache.buckets[bid].lock);
}


