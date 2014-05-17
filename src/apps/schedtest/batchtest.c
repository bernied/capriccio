
/**
 * test prog - should benefit from batching of stages
 **/


// FIXME: this currently works only with the yield_point blocking graph

#ifndef DEBUG_batchtest_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif

#include <threadlib.h>

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

#define LINESIZE 256 
#define DOREPS(x) DO256(x)

//#define DATASIZE 1024*512
#define DATASIZE 1024*1024*4
#define STEPSIZE 64

static char data0[DATASIZE];
static char data1[DATASIZE];
static char data2[DATASIZE];
static char data3[DATASIZE];
static char data4[DATASIZE];
static char data5[DATASIZE];
static char data6[DATASIZE];
static char data7[DATASIZE];
static char data8[DATASIZE];
static char data9[DATASIZE];

void* child2(void *arg)
{
  register char *p;
  (void) arg;

  DO32(
  p=data0;  DOREPS((*p)++;  p+=LINESIZE;);  thread_yield();
  p=data1;  DOREPS((*p)++;  p+=LINESIZE;);  thread_yield();
  p=data2;  DOREPS((*p)++;  p+=LINESIZE;);  thread_yield();
  p=data3;  DOREPS((*p)++;  p+=LINESIZE;);  thread_yield();
  p=data4;  DOREPS((*p)++;  p+=LINESIZE;);  thread_yield();
  p=data5;  DOREPS((*p)++;  p+=LINESIZE;);  thread_yield();
  p=data6;  DOREPS((*p)++;  p+=LINESIZE;);  thread_yield();
  p=data7;  DOREPS((*p)++;  p+=LINESIZE;);  thread_yield();
  p=data8;  DOREPS((*p)++;  p+=LINESIZE;);  thread_yield();
  p=data9;  DOREPS((*p)++;  p+=LINESIZE;);  thread_yield();
  )
  return NULL;
}

void* child(void *arg)
{
  register unsigned int i;
  //register unsigned int foo;
  (void) arg;

  // stage 1
  for(i=0; i<DATASIZE; i+=STEPSIZE)
    data1[i]++;;
  thread_yield();
  
  // stage 2
  for(i=0; i<DATASIZE; i+=STEPSIZE)
    data2[i]++;;
  thread_yield();
  
  // stage 3
  for(i=0; i<DATASIZE; i+=STEPSIZE)
    data3[i]++;;
  thread_yield();
  
  // stage 4
  for(i=0; i<DATASIZE; i+=STEPSIZE)
    data4[i]++;;
  thread_yield();

  // stage 1
  for(i=0; i<DATASIZE; i+=STEPSIZE)
    data1[i]++;;
  thread_yield();
  
  // stage 2
  for(i=0; i<DATASIZE; i+=STEPSIZE)
    data2[i]++;;
  thread_yield();
  
  // stage 3
  for(i=0; i<DATASIZE; i+=STEPSIZE)
    data3[i]++;;
  thread_yield();
  
  // stage 4
  for(i=0; i<DATASIZE; i+=STEPSIZE)
    data4[i]++;;
  thread_yield();

  // stage 1
  for(i=0; i<DATASIZE; i+=STEPSIZE)
    data1[i]++;;
  thread_yield();
  
  // stage 2
  for(i=0; i<DATASIZE; i+=STEPSIZE)
    data2[i]++;;
  thread_yield();
  
  // stage 3
  for(i=0; i<DATASIZE; i+=STEPSIZE)
    data3[i]++;;
  thread_yield();
  
  // stage 4
  for(i=0; i<DATASIZE; i+=STEPSIZE)
    data4[i]++;;
  thread_yield();

  // stage 1
  for(i=0; i<DATASIZE; i+=STEPSIZE)
    data1[i]++;;
  thread_yield();
  
  // stage 2
  for(i=0; i<DATASIZE; i+=STEPSIZE)
    data2[i]++;;
  thread_yield();
  
  // stage 3
  for(i=0; i<DATASIZE; i+=STEPSIZE)
    data3[i]++;;
  thread_yield();
  
  // stage 4
  for(i=0; i<DATASIZE; i+=STEPSIZE)
    data4[i]++;;
  thread_yield();

  return NULL;
}





int main(int argc, char **argv)
{
  int i;
  (void) argc; (void) argv;

  // spawn a bunch of threads
  for(i=0; i<1000; i++)
    thread_spawn("child", child2, (void*)i);

  return 0;
}
