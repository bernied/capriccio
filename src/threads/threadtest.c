/*
#define thc_test_and_set(var, goodval, newval)  \
   (  ((var) == (goodval)) ?                    \
      (((var) = (newval)), 1) :                 \
      0 )
*/

#include "threadlib.h"
#include "blocking_graph.h"
#include "util.h"


#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sched.h>
#include <signal.h>
#include <sys/types.h>

#ifndef DEBUG_threadtest_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif



//////////////////////////////////////////////////////////////////////
// test the speed of getpid()
//////////////////////////////////////////////////////////////////////

#define REPS 1
void getpid_test()
{
  register int ret;
  register unsigned long long start, end;
  register int i;
  unsigned long long total;

  for(i=0; i<1000; i++) {
    GET_CPU_TICKS(start);
    ret = getpid();
    GET_CPU_TICKS(end);
  }

  total = 0;
  for(i=0; i<REPS; i++) {
    GET_CPU_TICKS(start);
    ret = getpid();
    GET_CPU_TICKS(end);
    total += (end-start);
  }

  printf("ticks / getpid(): %lld\n", total / REPS);

  total = 0;
  for(i=0; i<REPS; i++) {
    GET_CPU_TICKS(start);
    GET_CPU_TICKS(end);
    total += (end-start);
  }
  
  printf("overhead ticks:   %lld\n", total / REPS);
}



//////////////////////////////////////////////////////////////////////
// test out the Linux clone() system call
//////////////////////////////////////////////////////////////////////

int cloned_child(void *arg)
{
  int num = (int) arg;

  printf("in cloned_child %d  (pid=%d)\n", num, getpid());
  
  return 0;
}

#define CLONE_THREAD_OPTS (\
   CLONE_FS | \
   CLONE_FILES | \
   CLONE_SIGHAND | \
   CLONE_PTRACE | \
   CLONE_VM | \
   0)

//   CLONE_THREAD |

#define STACKSIZE 2048

void clonetest()
{
  int i;
  int ret;
  char *stack;
  
  for(i=0; i<10; i++) {
    stack = malloc(STACKSIZE);  assert(stack);
    ret = clone(cloned_child, stack+STACKSIZE-4, CLONE_THREAD_OPTS, (void*)i);
    if(ret == -1) {
      perror("clone"); 
      exit(1);
    }
  }
}



//////////////////////////////////////////////////////////////////////
// to show stack smashing w/ stdio
//////////////////////////////////////////////////////////////////////

void* foo(void *arg)
{
  (void) arg;
  debug("about to print FOO\n");
  printf("FOO 1\n");
  debug("done printing FOO\n");
  thread_exit(0);
  return NULL;
}

void* spinner(void *arg)
{
  (void) arg;
  while(1) {
    thread_yield();
  }
}

void stacksmash(void)
{
  // these cause problems!!!
  debug("calling setbuf()\n");
  setbuf(stdout, NULL);
  setbuf(stderr, NULL);

  debug("spawning foo\n");
  thread_spawn("spinner", spinner, NULL);
  thread_spawn("foo", foo, NULL);
  debug("foo spawned\n");

  debug("about to yield\n");
  thread_yield();
  thread_yield();
  thread_yield();
  thread_yield();
  thread_exit_program(0);
}


//////////////////////////////////////////////////////////////////////
// for blocking graph creation tests
//////////////////////////////////////////////////////////////////////


void dummy(void)
{
  // make a node in the blocking graph
  bg_auto_set_node();
}



//////////////////////////////////////////////////////////////////////
// simple test of thread yielding
//////////////////////////////////////////////////////////////////////

void* child(void *arg)
{
  (void) arg;// suppress warning

  printf("%s: yielding  (%d)\n", thread_name(thread_self()), (int)arg);
  thread_yield();
  printf("%s: middle\n", thread_name(thread_self()));
  thread_yield();
  //thread_exit_program(0);
  printf("%s: done\n", thread_name(thread_self()));
  
  return NULL;
}


//////////////////////////////////////////////////////////////////////
// main
//////////////////////////////////////////////////////////////////////


int main(int argc, char **argv)
{
  int i;
  (void) argc;// suppress warning
  (void) argv;// suppress warning

  thread_yield();

  clonetest();

  getpid_test();

  //////////////////////////////////////////////////
  // debug printf() problem
  //////////////////////////////////////////////////
  //stacksmash();


  //////////////////////////////////////////////////
  // testing of node stats
  //////////////////////////////////////////////////
  thread_yield();  // to init the threads lib
  for(i=0; i<20; i++) {
    bg_auto_set_node();
    {
      char a[8];
      char *b = alloca(12);
      (void) b;
      a[1] = 0;
      bg_auto_set_node();
    }
    bg_auto_set_node();
    dummy();
  }
  bg_auto_set_node();

  //thread_exit_program(0);

  //////////////////////////////////////////////////
  // general testing
  //////////////////////////////////////////////////

  thread_spawn("child 1", child, (void*)234);
  thread_spawn("child 2", child, (void*)333);
  //thread_spawn("child 3", child, (void*)434);


  // yield control to the scheduler
  printf("main yielding\n");
  thread_yield();
  kill(getpid(), SIGUSR1);


  printf("back in main - main exiting\n");
  return 0;
}

