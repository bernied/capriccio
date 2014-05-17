
/**
 * test prog - should benefit from batching of stages
 **/


// FIXME: this currently works only with the yield_point blocking graph

#ifndef DEBUG_cachetest_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif

#include <stdlib.h>
#include <util.h>
#include <unistd.h>
#include <sys/mman.h>

#define DO1(x) x
#define DO8(x) x x x x x x x x
#define DO16(x) DO8(x) DO8(x) 
#define DO32(x) DO16(x) DO16(x)
#define DO64(x) DO32(x) DO32(x) 
#define DO128(x) DO64(x) DO64(x) 
#define DO256(x) DO128(x) DO128(x) 
#define DO512(x) DO256(x) DO256(x) 
#define DO1K(x) DO512(x) DO512(x) 
#define DO8K(x) DO1K(x) DO1K(x) DO1K(x) DO1K(x) DO1K(x) DO1K(x) DO1K(x) DO1K(x) 
#define DO64K(x) DO8K(x) DO8K(x) DO8K(x) DO8K(x) DO8K(x) DO8K(x) DO8K(x) DO8K(x) 



// cache info for homegate
//    L1 cache: 16384 bytes 6.43 nanoseconds 32 linesize 3.00 parallelism
//    L2 cache: 131072 bytes 23.64 nanoseconds 32 linesize 2.51 parallelism
//    Memory latency: 272.29 nanoseconds 2.87 parallelism

// lmbench cache info for largo
//    L1 cache: 8192 bytes 1.00 nanoseconds 64 linesize 1.65 parallelism
//    L2 cache: 524288 bytes 9.21 nanoseconds 128 linesize 1.22 parallelism
//    Memory latency: 156.80 nanoseconds 1.56 parallelism


// largo 16 reps, 256 line:  44x improvement


#define LINESIZE 256 
#define REPS 16
#define DOREPS(x) DO16(x)



#define SETS 100


#define DATASIZE 1024*1024*4
char data1[DATASIZE];
char data2[DATASIZE];
char data3[DATASIZE];
char data4[DATASIZE];


// from clearcache.c
void clearcache();

unsigned long long func(char *data)
{
  register char *p = data;
  register unsigned long long start, end;

  get_cpu_ticks(start);
  
  //DOREPS(if(*p >=1 )(*p)--; else (*p)++;  p+=LINESIZE;)
  DOREPS((*p)++;  p+=LINESIZE;)

  get_cpu_ticks(end);
  
  //output("functime: %lld\n", end-start);

  return end - start;
}


// make sure all data pages exist, and are locked in memory
void mlock_data_pages()
{
  unsigned long long start, end;

  get_cpu_ticks(start);
  if(mlock(data1, DATASIZE) != 0) fatal("mlock failed\n");
  if(mlock(data1, DATASIZE) != 0) fatal("mlock failed\n");
  if(mlock(data1, DATASIZE) != 0) fatal("mlock failed\n");
  if(mlock(data1, DATASIZE) != 0) fatal("mlock failed\n");
  get_cpu_ticks(end);

  printf("ticks to lock pages: %lld\n", end-start);
}

void munlock_data_pages()
{
  if(munlock(data1, DATASIZE) != 0) fatal("munlock failed\n");
  if(munlock(data2, DATASIZE) != 0) fatal("munlock failed\n");
  if(munlock(data3, DATASIZE) != 0) fatal("munlock failed\n");
  if(munlock(data4, DATASIZE) != 0) fatal("munlock failed\n");
}


#define PAGESIZE 1024*4
//#define TOUCHSKIP PAGESIZE
#define TOUCHSKIP 64

void touch_all_data()
{
  register char *p;
  register char *end;
  unsigned long long start, endticks;
  
  get_cpu_ticks(start);
  
  p = data1;  end = data1+DATASIZE;
  while(p < end)
    (*p)++,  p+= TOUCHSKIP;

  p = data2;  end = data2+DATASIZE;
  while(p < end)
    (*p)++,  p+= TOUCHSKIP;

  p = data3;  end = data3+DATASIZE;
  while(p < end)
    (*p)++,  p+= TOUCHSKIP;

  p = data4;  end = data4+DATASIZE;
  while(p < end)
    (*p)++,  p+= TOUCHSKIP;

  get_cpu_ticks(endticks);

  printf("ticks to touch pages: %lld\n", endticks-start);
}



int main(int argc, char **argv)
{
  register unsigned int j;
  unsigned long long cold_ticks, warm_ticks;

  if(argc != 1)
    fatal("format: %s\n", argv[0]);

  // FIXME: very strange.  touch_all_pages() takes 20% - 50% longer
  // after mlock_data_pages().  Moreover, munlock_data_pages() doesn't
  // return the timing to normal!!

  // make sure the pages are in memory
  func(data1);

  cold_ticks = warm_ticks = 0;
  for(j=0; j<128; j++) {
    clearcache(); 
    usleep(1);
    cold_ticks += func(data1); 
    DO16(func(data1););
    warm_ticks += func(data1);
  }
  cold_ticks /= 128;
  warm_ticks /= 128;

  //DO128(totaltime+=func(data1););

  printf("\n");
  printf("cache miss:   %lld\n", cold_ticks);
  printf("cache hit:    %lld\n", warm_ticks);
  printf("gain:         %.1fx\n", (float)cold_ticks / (float)warm_ticks);
  exit(0);
  
  /*
  flush_both_caches();
  totaltime = 0;
  for(j=1; j<SETS; j++)
    totaltime += func(data1);
  for(j=1; j<SETS; j++)
    totaltime += func(data2);
  for(j=1; j<SETS; j++)
    totaltime += func(data3);
  for(j=1; j<SETS; j++)
    totaltime += func(data4);
  printf("GOOD: time / access: %2.0f\n", (double)(totaltime) / (double)(4 * REPS * SETS));


  flush_both_caches();
  for(j=1; j<SETS; j++) {
    totaltime += func(data1);
    totaltime += func(data2);
    totaltime += func(data3);
    totaltime += func(data4);
  }
  printf("BAD:  time / access: %2.0f\n", (double)(totaltime) / (double)(4 * REPS * SETS));

  return 0;

  */

  /*
  // warmup 
  for(j=0; j<10; j++)
    for(i=0; i<sizeof(data); i+=step)
      data[i]++;

  // timing run
  get_cpu_ticks(start);
  for(j=0; j<REPS; j++)
    for(i=0; i<sizeof(data); i+=step)
      data[i]++;
  get_cpu_ticks(end);

  printf("time: %lld\n", end-start);
  printf("time / access: %2.0f\n", 
         (double)(end-start) / (double)(REPS * sizeof(data)/step));
 
  return 0;
  */
}





