
COUNTLINES_SKIP := ./libaio  ./epoll-lib   *test.c   ./tests

include ../Make.include


ifeq '$(HAVE_SYS_EPOLL)' '1'
IODEFS := $(IODEFS) -DHAVE_SYS_EPOLL
EPOLLLIB = -lepoll
EPOLL_STATIC_LIB = /usr/lib/libepoll.a
endif

ifeq '$(HAVE_AIO)' '1'
IODEFS := $(IODEFS) -DHAVE_AIO
DISKIOLIB = -laio
DISKIO_STATIC_LIB = /usr/lib/libaio.a
endif

ifeq '$(USE_PERFCTR)' '1'
PERFCTRLIB = -lperfctr
#PERFCTR_STATIC_LIB = /usr/lib/libperfctr-debug.a
PERFCTR_STATIC_LIB = /usr/lib/libperfctr.a
endif

CC := $(CC) $(CFLAGS) $(IODEFS)

INC = -I$(UTIL) -I$(THREAD)
LIB := -L../lib -lpthread $(DISKIOLIB) $(EPOLLLIB) $(PERFCTRLIB)

TESTPROG  = aiotest
SHAREDTESTPROG  = aiotest.shared
CFILES = diskio_kthread.c \
         blocking_io.c  diskio_blocking.c     sockio_epoll.c \
         diskio_aio.c   diskio_immediate.c    sockio_poll.c


OBJS    := $(patsubst %.c, %.o, $(CFILES))
HEADERS = $(wildcard *.h)

LIBPTHREAD = ../lib/libpthread.a
LIBPTHREADSHARED = ../lib/libpthread.so
LIBPTHREADSHAREDLINK = ../lib/libpthread.so.0

all: lib

lib: $(LIBPTHREAD) $(LIBPTHREADSHARED)

sharedlib: $(LIBPTHREADSHARED)

# This static library combines all libs
$(LIBPTHREAD): $(OBJS) $(LIBUTIL) $(LIBTHREAD) $(LIBSTACK)
	rm -rf __tmp
	mkdir __tmp
	cp $(OBJS) __tmp;
	cd __tmp; ar x $(LIBTHREAD)
	cd __tmp; ar x $(LIBUTIL)
	cd __tmp; ar x $(LIBSTACK)
	cd __tmp; ar x /usr/lib/libm.a
	if [ "$(DISKIO_STATIC_LIB)" != "" ]; then cd __tmp; ar x $(DISKIO_STATIC_LIB); fi
	if [ "$(EPOLL_STATIC_LIB)" != "" ]; then cd __tmp; ar x $(EPOLL_STATIC_LIB); fi
	if [ "$(PERFCTR_STATIC_LIB)" != "" ]; then cd __tmp; ar x $(PERFCTR_STATIC_LIB); fi
	$(AR) rc $@ __tmp/*
	ranlib $@
	rm -rf __tmp


$(LIBPTHREADSHARED): $(OBJS) $(LIBUTIL) $(LIBTHREAD) $(LIBSTACK)
	rm -rf __tmp
	mkdir __tmp
	cp $(OBJS) __tmp;
	cd __tmp; ar x $(LIBTHREAD)
	cd __tmp; ar x $(LIBUTIL)
	cd __tmp; ar x $(LIBSTACK)
	rm -f $@
	gcc -shared -Wl,-soname,libpthread.so.0 -Wl,--version-script=Versions.def -Wl,--enable-new-dtags,-z,nodelete -o $@ __tmp/* $(EPOLLLIB) $(DISKIOLIB) $(PERFCTRLIB) -lc
	rm -f $(LIBPTHREADSHAREDLINK)
	ln -s $(LIBPTHREADSHARED) $(LIBPTHREADSHAREDLINK)
	rm -rf __tmp

# This causes strdup, malloc, etc. in our initializers to fail
#	-Wl,-z,initfirst


$(OBJS) : %.o : %.c $(HEADERS) $(MAKEDEPS)
	@./check_syscall $<
	$(CC) $(INC) -o $@ -c $<

$(TESTPROG) : % : %.c $(LIBPTHREAD) $(HEADERS) $(MAKEDEPS)
	$(LD) -static $(INC) -o $@ $< $(LIB) -lm


$(SHAREDTESTPROG) : %.shared : %.c $(LIBPTHREADSHARED) $(HEADERS) $(MAKEDEPS)
	LD_LIBRARY_PATH=../lib $(CC) $(INC) -o $@ $< $(LIB) -lm

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
	rm -f *.o *.a *~ core* __test $(TESTPROG) $(SHAREDTESTPROG) $(LIBPTHREAD) $(LIBPTHREADSHARED) $(LIBPTHREADSHAREDLINK)

