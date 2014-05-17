
COUNTLINES_SKIP := ./libaio  ./epoll-lib   *test.c   ./tests

include ../Make.include

IODEFS := $(IODEFS) -DHAVE_SYS_EPOLL
EPOLLLIB = -lepoll
EPOLL_STATIC_LIB = /usr/lib/libepoll.a

IODEFS := $(IODEFS) -DHAVE_AIO
DISKIOLIB = -laio
DISKIO_STATIC_LIB = /usr/lib/libaio.a

ifeq '$(USE_PERFCTR)' '1'
PERFCTRLIB = -lperfctr
#PERFCTR_STATIC_LIB = /usr/lib/libperfctr-debug.a
PERFCTR_STATIC_LIB = /usr/lib/libperfctr.a
endif

CC := $(CC) $(CFLAGS) $(IODEFS)

INC = -I$(UTIL) -I$(THREAD) -I../threads/coro-1.1.0.pre2/arch/x86-linux/
LIB := -L. -L$(THREAD) -L$(UTIL) -L$(STACK) -lasyncio -lthread -lstack -lutil $(DISKIOLIB) $(EPOLLLIB) $(PERFCTRLIB)

SHAREDAIO = libasyncio.so
TESTPROG  = aiotest
SHAREDTESTPROG  = aiotest.shared
CFILES = $(filter-out aiotest.c, $(wildcard *.c))


OBJS    := $(patsubst %.c, %.o, $(CFILES))
HEADERS = $(wildcard *.h)

LIBPTHREAD = ../lib/libpthread.a
LIBPTHREADSHARED = ../lib/libpthread.so
LIBPTHREADSHAREDLINK = ../lib/libpthread.so.0

all: lib

lib: $(LIBAIO) $(LIBPTHREAD) $(LIBPTHREADSHARED)

sharedlib: $(LIBPTHREADSHARED)
#sharedlib: $(SHAREDAIO)

# This static library combines all libs
$(LIBPTHREAD): $(LIBAIO) $(LIBUTIL) $(LIBTHREAD) $(LIBSTACK)
	rm -rf __tmp
	mkdir __tmp
	cd __tmp; ar x $(LIBAIO)
	cd __tmp; ar x $(LIBTHREAD)
	cd __tmp; ar x $(LIBUTIL)
	cd __tmp; ar x $(LIBSTACK)
	if [ "$(DISKIO_STATIC_LIB)" != "" ]; then cd __tmp; ar x $(DISKIO_STATIC_LIB); fi
	if [ "$(EPOLL_STATIC_LIB)" != "" ]; then cd __tmp; ar x $(EPOLL_STATIC_LIB); fi
	if [ "$(PERFCTR_STATIC_LIB)" != "" ]; then cd __tmp; ar x $(PERFCTR_STATIC_LIB); fi
	ar rcs $@ __tmp/*
	ranlib $@
	rm -rf __tmp

$(LIBPTHREADSHARED): $(LIBAIO) $(LIBUTIL) $(LIBTHREAD) $(LIBSTACK)
	rm -rf __tmp
	mkdir __tmp
	cd __tmp; ar x $(LIBAIO)
	cd __tmp; ar x $(LIBTHREAD)
	cd __tmp; ar x $(LIBUTIL)
	cd __tmp; ar x $(LIBSTACK)
	rm -f $@
	gcc -shared -Wl,-soname,libpthread.so.0 -Wl,--version-script=../aio/Versions.def -Wl,--enable-new-dtags,-z,nodelete -o $@ __tmp/* $(EPOLLLIB) $(DISKIOLIB) $(PERFCTRLIB) -lc
	rm -f $(LIBPTHREADSHAREDLINK)
	ln -s $(LIBPTHREADSHARED) $(LIBPTHREADSHAREDLINK)
	rm -rf __tmp

$(LIBAIO): $(OBJS)
	ar rcs $@ $^ 

$(SHAREDAIO): $(OBJS) $(DISKIO_STATIC_LIB) $(LIBTHREAD) $(LIBUTIL) $(EPOLL_STATIC_LIB) 
	rm -f $(SHAREDAIO)*
	gcc -shared -Wl,-soname,$(SHAREDAIO).1 -Wl,--enable-new-dtags,-z,nodelete -Wl,--enable-new-dtags,-z,initfirst -o $(SHAREDAIO).1 -Wl,-whole-archive $^ -Wl,-no-whole-archive -ldl $(DISKIOLIB)
	ln -s $(SHAREDAIO).1 $(SHAREDAIO)


$(OBJS) : %.o : %.c $(HEADERS) $(MAKEDEPS)
	$(CC) $(INC) -o $@ -c $<

$(TESTPROG) : % : %.c $(LIBAIO) $(LIBTHREAD) $(LIBUTIL) $(HEADERS) $(MAKEDEPS)
	$(LD) -static $(INC) -o $@ $< $(LIB) -lm


$(SHAREDTESTPROG) : %.shared : %.c $(SHAREDAIO) $(HEADERS) $(MAKEDEPS)
	LD_LIBRARY_PATH=. $(CC) $(INC) -o $@ $< $(LIB) -lm

test: $(TESTPROG)
	./$(TESTPROG)

sharedtest: $(SHAREDTESTPROG)
	./$(SHAREDTESTPROG)

# rule to check the utility library
$(LIBUTIL): FORCE
	/bin/sh -c "cd $(UTIL); make"
#	$(MAKE) -C $(UTIL)

# rule to check the thread library
$(LIBTHREAD): FORCE
	@$(MAKE) -s -C $(THREAD)

# rule to check the thread library
$(LIBSTACK): FORCE
	@$(MAKE) -s -C $(STACK)

FORCE:

clean:
	rm -f *.o *.a *~ core* __test $(TESTPROG) $(SHAREDTESTPROG) $(LIBAIO) $$(LIBPTHREAD) $(LIBPTHREADSHARED) $(LIBPTHREADSHAREDLINK)


