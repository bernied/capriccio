/*
    Sample code for coroutine implementation.

    The main porting points:

    - How to allocate the stack?  mmap or malloc or something else.
      Requires adjustments in co_create and co_delete.
    - Check fatal.  It should generate a segv or something that
      is easily caught in a debugger.
    - Implement co_call.
    - Implement __co_wrap.
    - Adjust co_create to setup the stack for the first co_call to call wrap.

    Maybe you have to implement parts (co_call, __co_wrap) in an .s file.
*/

#include <unistd.h>		/* for write, mmap, munmap */
#include <sys/mman.h>		/* for mmap, munmap */
#include "coro.h"		/* for struct coroutine */

/* some compiler checks */
#if !defined(i386)
#error For x86-CPUs only
#endif
#if !defined(__GNUC__)
#warning May break without gcc.  Be careful.
#endif

#ifndef PAGESIZE
#define PAGESIZE	4096
#endif

struct coroutine co_main[1] = { { 0 } };
struct coroutine *co_current = co_main;


/* make sure that fatal generates a segv.  it mustn't return! */
#define fatal(msg) \
    for (;;)						\
    {							\
	write(2, "coro: " msg "\r\n", sizeof(msg)+7);	\
	*(unsigned int *)0 = 0xfee1dead;		\
    }



/*
    Create new coroutine.
    'func' is the entry point
    'stack' is the start of the coroutines stack.  if 0, one is allocated.
    'size' is the size of the stack
*/


/* !!! special calling convention. !!! */
static void
__co_wrap(void *data) /* arg in the register where results are returned */
{
    co_current->resumeto = co_current->caller;

    for (;;)
	data = co_resume(co_current->func(data));
}

/* !!! use mmap or malloc.  setup stack for co_call. !!! */
struct coroutine *
co_create(void *func, void *stack, int size)
{
    struct coroutine *co;
    int to_free = 0;

    if (size < 128)
	return 0;

    if (stack == 0)
    {
	/* align stacksize */
	size += PAGESIZE-1;
	size &= ~(PAGESIZE-1);
#if USE_MMAP
	stack = mmap(0, size, PROT_READ|PROT_WRITE,
			      MAP_PRIVATE|MAP_ANON, -1, 0);
	if (stack == (void*)-1)
	    return 0;
#else /* malloc */
	size -= 32;			/* some place for malloc to waste */
	stack = malloc(size);
	if (stack == 0)
	    return 0;
#endif
	to_free = size;
    }
    co = stack + size;
    (unsigned long)co &= ~3;	// align
    co -= 1;

    /* initialize struct coroutine */
    co->sp = co;
    co->caller = 0;
    co->resumeto = 0;
    co->user = 0;
    co->func = func;
    co->to_free = to_free;

    /*
	Setup stack so that a pop_register_vars and a return starts
	the wrapper.  It is started by a return, so its single arg
	is passed in the return register.
    */
    *--(void **)co->sp = __co_wrap;	// return addr (here: start addr)
    *--(void **)co->sp = 0;		// reg1
    *--(void **)co->sp = 0;		// reg2
    *--(void **)co->sp = 0;		// ...
    return co;
}



/*
    delete a coroutine.
*/

void
co_delete(struct coroutine *co)
{
    if (co == co_current)
	fatal("coroutine deletes itself");

    if (co->to_free)
#if USE_MMAP
	munmap((void*)co + sizeof(*co) - co->to_free, co->to_free);
#else /* malloc */
	free((void*)co + sizeof(*co) - co->to_free);
#endif
}



/*
    delete self and switch to 'new_co' passing 'data'
*/

static void *helper_args[2];

static void
del_helper(void **args)
{
    for (;;)
    {
	if (args != helper_args)
	    fatal("resume to deleted coroutine");
	co_delete(co_current->caller);
	args = co_call(args[0], args[1]);
    }
}

void
co_exit_to(struct coroutine *new_co, void *data)
{
    static struct coroutine *helper = 0;
    static char stk[256];

    helper_args[0] = new_co;
    helper_args[1] = data;

    if (helper == 0)
	helper = co_create(del_helper, stk, sizeof(stk));

    /* we must leave this coroutine.  so call the helper. */
    co_call(helper, helper_args);
    fatal("stale coroutine called");
}

void
co_exit(void *data)
{
    co_exit_to(co_current->resumeto, data);
}



/*
    Call other coroutine.
    'new_co' is the coroutine to switch to
    'data' is passed to the new coroutine
*/

/* !!! pseudo code !!! */
void *
co_call(struct coroutine *new_co, void *data)
{
    push_all_register();
    co_current->sp = stack_pointer;

    new_co->caller = co_current;
    co_current = new_co;

    stack_pointer = co_current->sp;
    pop_all_registers();
    return data;
}

void *
co_resume(void *data)
{
    data = co_call(co_current->resumeto, data);
    co_current->resumeto = co_current->caller;
    return data;
}
