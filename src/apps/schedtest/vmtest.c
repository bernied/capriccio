
/**
 * test prog - should benefit from batching of stages
 **/


// FIXME: this currently works only with the yield_point blocking graph

#ifndef DEBUG_vmtest_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif

#include <stdlib.h>
#include <util.h>
#include <unistd.h>


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

#define PAGESIZE 1024*4
//#define TOUCHSKIP PAGESIZE
#define TOUCHSKIP 64


#define DATASIZE 1024*1024*768
char data1[DATASIZE];
char data2[DATASIZE];


unsigned long long func(char *data)
{
  register char *p = data;
  unsigned long long start, end;

  get_cpu_ticks(start);

  while(p < data+DATASIZE)
    (*p)++, p+=PAGESIZE;

  get_cpu_ticks(end);
  
  //output("functime: %lld\n", end-start);

  return end - start;
}



int main(int argc, char **argv)
{
  unsigned long long cold_ticks, warm_ticks;

  if(argc != 1)
    fatal("format: %s\n", argv[0]);

  func(data1);  // touch pages
  func(data2);  // force out to swap

  cold_ticks = func(data1);  // force out to swap, and bring back
  warm_ticks = func(data1);  // all in memory

  printf("\n");
  printf("cache miss:   %lld\n", cold_ticks);
  printf("cache hit:    %lld\n", warm_ticks);
  printf("gain:         %.1fx\n", (float)cold_ticks / (float)warm_ticks);
  exit(0);
  
}





