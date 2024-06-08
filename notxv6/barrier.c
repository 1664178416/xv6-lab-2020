#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

static int nthread = 1;
static int round = 0;

// 互斥锁，条件变量，到达屏障的线程数、轮数
struct barrier {
  pthread_mutex_t barrier_mutex;
  pthread_cond_t barrier_cond;
  int nthread;      // Number of threads that have reached this round of the barrier
  int round;     // Barrier round
} bstate;

//初始化屏障
static void
barrier_init(void)
{
  assert(pthread_mutex_init(&bstate.barrier_mutex, NULL) == 0);
  assert(pthread_cond_init(&bstate.barrier_cond, NULL) == 0);
  bstate.nthread = 0;
}

//屏障函数，等待实现
static void 
barrier()
{
  // YOUR CODE HERE
  //
  // Block until all threads have called barrier() and
  // then increment bstate.round.
  //
  //申请锁，互斥操作
  pthread_mutex_lock(&bstate.barrier_mutex);
  // judge whether all threads reach the barrier
  if(++bstate.nthread != nthread)  {    // not all threads reach    
    pthread_cond_wait(&bstate.barrier_cond,&bstate.barrier_mutex);  // wait other threads
  } else {  // all threads reach
    bstate.nthread = 0; // reset nthread
    ++bstate.round; // increase round
    pthread_cond_broadcast(&bstate.barrier_cond);   // wake up all sleeping threads
  }
  pthread_mutex_unlock(&bstate.barrier_mutex);

}

//每个线程执行的函数
static void *
thread(void *xa)
{
  long n = (long) xa;
  long delay;
  int i;

  for (i = 0; i < 20000; i++) {
    int t = bstate.round;
    //检查是否实现了所有线程共同达到屏障的效果
    assert (i == t);
    //等待所有线程到达屏障
    barrier();
    usleep(random() % 100);
  }

  return 0;
}

int
main(int argc, char *argv[])
{
  pthread_t *tha;
  void *value;
  long i;
  double t1, t0;

  if (argc < 2) {
    fprintf(stderr, "%s: %s nthread\n", argv[0], argv[0]);
    exit(-1);
  }
  //参数指定线程数量
  nthread = atoi(argv[1]);
  tha = malloc(sizeof(pthread_t) * nthread);
  // srandom 是 C 标准库中的一个函数，用于设置伪随机数生成器（PRNG）的起始种子 -- 输出只是伪随机而不是真正的随机数
  srandom(0);

  barrier_init();

  //创建n个线程执行
  for(i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, thread, (void *) i) == 0);
  }
  for(i = 0; i < nthread; i++) {
    assert(pthread_join(tha[i], &value) == 0);
  }
  printf("OK; passed\n");
}
