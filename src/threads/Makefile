
COUNTLINES_SKIP := ./coro-1.1.0.pre2 *test.c

include ../Make.include

COROMAIN=coro-1.1.0.pre2
CORO=$(COROMAIN)/arch/x86-linux
COROOBJ=$(CORO)/coro.o

INC=-I$(CORO) -I$(UTIL)
#LIB=-L. -L$(UTIL) -lthread -lutil -lstack
LIB=$(LIBTHREAD) $(LIBUTIL) $(LIBSTACK) $(PERFCTRLIB)

LIBTHREAD = libthread.a
TESTPROGS  = threadtest pthreadtest clonetest pidtest

#CFILES := $(filter-out %test.c, $(wildcard *.c))
CFILES = blocking_graph.c  sched_graph_priority.c  threadlib.c \
         mutex.c           resource_stats.c   sched_graph_rr.c \
         pthread.c         sched_global_rr.c  signals.c        \
         readproc.c

OBJS   := $(patsubst %.c, %.o, $(CFILES))
HEADERS := $(wildcard *.h)

all: $(LIBTHREAD)

$(LIBTHREAD): $(COROOBJ) $(OBJS)
	$(AR) rc $@ $^

$(COROOBJ):
	@cd $(COROMAIN); if [ ! -f Makefile ]; then /bin/sh ./configure --arch arch/x86-linux; fi; make

%.o: %.c $(HEADERS) $(MAKEDEPS)
	$(CC) $(CFLAGS) $(INC) -o $@ -c $<

$(TESTPROGS) : % : %.c $(LIBTHREAD) $(LIBUTIL) $(LIBSTACK) $(HEADERS) $(MAKEDEPS)
	$(LD) $(CFLAGS) $(INC) -o $@ $< $(LIB)

test: threadtest
	./threadtest

ptest: pthreadtest
	./pthreadtest

# rule to check the utility library
$(LIBUTIL): FORCE
	@$(MAKE) -s -C $(UTIL)

# rule to check the utility library
$(LIBSTACK): FORCE
	@$(MAKE) -s -C $(STACK)

clean:
	rm -f *.o *.a *~ core $(TESTPROGS) $(COROMAIN)/Makefile $(CORO)/*.[oa]
	rm -rf *.i *cured.c *.browser *_comb.c *_combcil.c

FORCE:
