
COUNTLINES_SKIP := ./old *test.c

include ../Make.include


INC = -I.
LIB = -L. -lutil $(PERFCTRLIB)

LIBUTIL  = libutil.a
TESTPROG = utiltest

HEADERS = $(wildcard *.h)
#CFILES := $(filter-out $(TESTPROG).c, $(wildcard *.c))
#CFILES := $(filter-out serialization.c, $(wildcard *.c))

CFILES := clock.c  debug.c  linked_list.c  object_pool.c  occ_list.c  \
          plhash.c  timing.c config.c


OBJS := $(patsubst %.c, %.o, $(CFILES))

all: $(LIBUTIL)

$(LIBUTIL): $(OBJS)
	$(AR) rc $@ $^

%.o : %.c $(HEADERS) $(MAKEDEPS)
	$(CC) -D_REENTRANT $(CFLAGS) $(INC) -c -o $@ $<


clean:
	rm -f *.o *.a *~ core $(TESTPROG) occtest
	rm -rf *.i *cured.c *.browser *_comb.c *_combcil.c


test: $(TESTPROG)
	./$(TESTPROG)


occtest: occ_list.c $(MAKEDEPS) debug.o
	gcc -static -D_REENTRANT -g -DTEST_OCC_DONT_DEFINE -o occtest  occ_list.c debug.o


$(TESTPROG) : % : %.c $(HEADERS) $(LIBUTIL) $(MAKEDEPS)
	$(LD) $(CFLAGS) $(INC) -o $@ $< $(LIB)

#	$(CC) $(CFLAGS) $(INC) -o $@ $< $(LIBUTIL)






