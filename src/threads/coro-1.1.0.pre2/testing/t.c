#include <stdio.h>
#include <stdarg.h>
#include "coro.h"

/* some convenience macros to reduce casting needs */
#define co_call_v(c,d)          (void)  co_call((c), (void*)(d))
#define co_call_p(c,d)                  co_call((c), (void*)(d))
#define co_call_i(c,d)          (int)   co_call((c), (void*)(d))

#define co_resume_v(d)          (void)  co_resume((void*)(d))
#define co_resume_p(d)                  co_resume((void*)(d))
#define co_resume_i(d)          (int)   co_resume((void*)(d))

#define co_exit_to_v(c,d)       co_exit_to((c), (void*)(d))
#define co_exit_v(d)            co_exit((void*)(d))


#define STKSZ	4096

struct coroutine *c1, *c2, *c3, *c4, *c5, *c6;
int i1, i2, i3, i4, i5, i6, i7, i8;
char *s1, *s2, *s3, *s4, *s5, *s6;

char *tname = "init";

#define cstart() print("enter %s", tname = __FUNCTION__);
#define center() print("enter %s", __FUNCTION__);
#define cleave() print("leave %s", __FUNCTION__);

void
fatal(char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    fprintf(stderr, "FATAL %s: ", tname);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    exit(1);
}

void
error(char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    fprintf(stderr, "ERROR %s: ", tname);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
}

void
print(char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    fprintf(stdout, "%s: ", tname);
    vfprintf(stdout, fmt, args);
    fputc('\n', stdout);
}

static struct coroutine *
co_creat(void *func)
{
    struct coroutine *x;
    
    x = co_create(func, 0, STKSZ);
    if (x == 0)
	fatal("co_create failed");
    return x;
}

////////////////////////////////// t1 ////////////////////////////////////

// co_call

void
t1_1(int arg)
{
    center();
    co_call_v(co_main, arg+1);
    co_call_v(co_main, "Hello");
    co_call_v(co_main, "World!");
    cleave();
}

void
t1(void)
{
    cstart();

    c1 = co_creat(t1_1);
    i1 = co_call_i(c1, 42);
    s1 = co_call_p(c1, 0);
    s2 = co_call_p(c1, 0);
    print("%d %s %s", i1, s1, s2);

    co_delete(c1);
    cleave();
}

////////////////////////////////// t2 ////////////////////////////////////

// co_resume

void
t2_1(int arg)
{
    center();
    co_resume_v(arg+1);
    co_resume_v(co_call_p(c2, 0));	// co_call mustn't destry resume_to
    co_resume_v("World!");
    cleave();
}

void
t2_2(void *d)
{
    center();
    co_resume_v("Hello");
    cleave();
}

void
t2(void)
{
    cstart();

    c1 = co_creat(t2_1);
    c2 = co_creat(t2_2);
    i1 = co_call_i(c1, 43);
    s1 = co_call_p(c1, 0);
    s2 = co_call_p(c1, 0);
    print("%d %s %s", i1, s1, s2);

    co_delete(c2);
    co_delete(c1);
    cleave();
}

////////////////////////////////// t3 ////////////////////////////////////

// restart

int
t3_1(int arg)
{
    center();
    co_resume_v(arg*arg);
    cleave();
    return arg*arg*arg;
}

void
t3(void)
{
    cstart();

    c1 = co_creat(t3_1);
    i1 = co_call_i(c1, 3);
    i2 = co_call_i(c1, 0);
    i3 = co_call_i(c1, 5);
    i4 = co_call_i(c1, 0);
    print("%d %d %d %d", i1, i2, i3, i4);

    co_delete(c1);
    cleave();
}



////////////////////////////////// t4 ////////////////////////////////////

// calling self

void
t4_1(int arg)
{
    center();
    arg = co_call_i(c1, arg*2);	// co_call is a nop!
    arg = co_resume_i(arg);
    arg = co_call_i(c1, arg*7);	// co_call is a nop!
    co_resume_v(arg);
    cleave();
}

void
t4(void)
{
    cstart();

    c1 = co_creat(t4_1);
    i1 = co_call_i(c1, 21);
    i2 = co_call_i(c1, 6);
    print("%d %d", i1, i2);

    co_delete(c1);
    cleave();
}


////////////////////////////////// t5 ////////////////////////////////////

// preserve register vars

void
t5_1(int arg)
{
    register int r1,r2,r3,r4,r5,r6,r7,r8;
    register double f1,f2,f3,f4,f5,f6,f7,f8;

    center();

    /* populate registers */
    r1=i1*arg, r2=i2*arg, r3=i3*arg, r4=i4*arg;
    r5=i5+arg, r6=i6+arg, r7=i7+arg, r8=i8+arg;
    f1=i1*arg*.2, f2=i2*arg*.4, f3=i3*arg*.6, f4=i4*arg*.8;
    f5=i5+arg*1.2, f6=i6+arg*1.4, f7=i7+arg*1.6, f8=i8+arg*1.8;

    co_resume_v(1);
    print("%d %d %d %d %d %d %d %d", r1,r2,r3,r4,r5,r6,r7,r8);
    print("%g %g %g %g %g %g %g %g", f1,f2,f3,f4,f5,f6,f7,f8);
    co_resume_v(2);
    cleave();
}

void
t5(void)
{
    cstart();

    i1=1, i2=2, i3=3, i4=4, i5=5, i6=6, i7=7, i8=8;

    c1 = co_creat(t5_1);
    c2 = co_creat(t5_1);
    co_call_v(c1, 5);
    co_call_v(c2, 15);
    co_call_v(c1, 0);
    co_call_v(c2, 0);
    co_delete(c1);
    co_delete(c2);
    cleave();
}


////////////////////////////////// t6 ////////////////////////////////////

// proper stack usage

void
t6_1(int arg)
{
    center();

    for (;;)
	arg = co_resume_i(arg);

    cleave();
}

void
t6(void)
{
    cstart();

    c1 = co_creat(t6_1);
    for (i2 = i3 = 0; i2 < 10000; ++i2)
	i3 += co_call_i(c1, i2);
    print("%d", i3);

    co_delete(c1);
    cleave();
}



////////////////////////////////// t7 ////////////////////////////////////

// application provided unaligned stack

void
t7_1(int arg)
{
    int x = arg + 100;

    center();
    co_resume(&x);
    cleave();
}

void
t7(void)
{
    char stk[STKSZ];
    int *p1;

    cstart();

    c1 = co_create(t7_1, stk+1, STKSZ-3);
    p1 = co_call_p(c1, 23);
    if ((char *)p1 >= stk && (char *)p1 < stk + STKSZ)
	print("%d", *p1);
    else
	print("p1 outside stack");

    co_delete(c1);
    cleave();
}

////////////////////////////////// t8 ////////////////////////////////////

// co_exit_to

void
t8_1(int arg)
{
    center();
    co_call_v(co_main, arg*5);
    cleave();
}

void
t8_2(int arg)
{
    center();
    co_exit_to_v(c1,arg*2);
    cleave();
}

void
t8(void)
{
    cstart();

    c1 = co_creat(t8_1);
    c2 = co_creat(t8_2);
    i1 = co_call_i(c2, 123);
    print("%d", i1);

    co_delete(c1);
    cleave();
}



////////////////////////////////// t9 ////////////////////////////////////

// co_exit

void
t9_1(int arg)
{
    center();
    co_exit_v(arg+1000);
    cleave();
}

void
t9(void)
{
    cstart();

    c1 = co_creat(t9_1);
    i1 = co_call_i(c1, 42);
    print("%d", i1);

    cleave();
}





//////////////////////////////////////////////////////////////////////////

void
main(int argc, char **argv)
{
    setlinebuf(stdout);

    cstart();
    t1();
    t2();
    t3();
    t4();
    t5();
    t6();
    t7();
    t8();
    t9();
    cleave();
    exit(0);
}
