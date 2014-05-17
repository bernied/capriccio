#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <coro.h>

/*
    Example of coroutines building a data processing pipeline.
    That is, each coroutine performs a task on some data and
    calls one coroutine to get more data (read) and another one
    as an output routine (write).

    Understanding of how these routines work is left as an
    IMHO interesting exercise ;)  Only the framework of
    setting up the pipe is a little bit tricky, the usage
    (see source/sink/filter1/filter2) is trivial.
*/

#define in()		(int)co_call(co_in, 0)
#define out(ch)		(void)co_call(co_out, (void*)(ch))


static struct coroutine *
new(void *func, ...)
{
    va_list args;
    struct coroutine *co;
    
    if (!(co = co_create(func, 0, 8192))) {
	fprintf(stderr, "error creating coroutine\n");
	exit(1);
    }
    va_start(args, func);
    co_call(co, args);
    va_end(args);
    return co;
}

static struct coroutine *
pipe(struct coroutine *in, struct coroutine *out)
{
    out->user = in;
    co_call(in, out);
    co_call(out, in);
    return out;
}

static void
delete(struct coroutine *co)
{
    if (co) {
	delete(co->user);
	co_delete(co);
    }
}

static void *
run(struct coroutine *list)
{
    void *res = co_call(list, 0);
    delete(list);
    return res;
}

static void
source(va_list args)
{
    FILE *fp = va_arg(args, FILE *);
    struct coroutine *co_out = co_resume(0);

    co_resume(0);

    for (;;)
	out(fgetc(fp));
}

static void
sink(va_list args)
{
    FILE *fp = va_arg(args, FILE *);
    struct coroutine *co_in = co_resume(0);
    int ch;

    co_resume(0);

    while ((ch = in()) != EOF)
	fputc(ch, fp);

    co_resume("<done>");
}

static void
filter1(va_list args)
{
    struct coroutine *co_in = co_resume(0);
    struct coroutine *co_out = co_resume(0);

    co_resume(0);

    for (;;)
	out(toupper(in()));
}

static void
filter2(va_list args)
{
    struct coroutine *co_in = co_resume(0);
    struct coroutine *co_out = co_resume(0);
    int ch;

    co_resume(0);

    for (;;) {
	out(ch = in());
	if (isprint(ch)) {
	    out('\b');
	    out(ch);
	}
    }
}


int
main(int argc, char **argv)
{
    printf("%s\n",  (char*)
	run(pipe(pipe(pipe(new(source, stdin),
			    new(filter1)), new(filter2)), new(sink, stdout)))
	);
    exit(0);
}
