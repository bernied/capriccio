
include ../../Make.include

ifeq ($(SOCKIO)_x,epoll_x)
EPOLLLIB = -lepoll
endif
ifeq ($(DISKIO)_x,aio_x)
DISKIOLIB = -laio
endif

NEWLIBC = -L/opt/testing/lib
INC := -I$(SRCDIR)/include -I$(UTIL)
LIB := -L$(SRCDIR)/lib $(NEWLIBC)


SOURCES = $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)
PTHREAD_OBJECTS = $(SOURCES:.c=.po)
PTH_OBJECTS = $(SOURCES:.c=.ptho)
HEADERS = $(wildcard *.h)

all: foo knot knot.pthreads

foo:
	echo $(CC)

clean:
	rm -rf knot knot.pthreads knot.pth knot_comb.* *.o *.po *.ptho
	rm -rf knot.browser *.i *_combcured.c

%.o: %.c $(HEADERS) $(MAKEDEPS)
	$(CC) $(CFLAGS) $(INC) -o $@ -c $<

%.po: %.c $(HEADERS) 
	$(CC) $(PCFLAGS) -I$(UTIL) -o $@ -c $<

%.ptho: %.c $(HEADERS) 
	$(CC) $(PTHCFLAGS) -I$(UTIL) -o $@ -c $<


knot : $(OBJECTS) $(LIBAIO) $(LIBTHREAD) $(LIBUTIL) $(HEADERS) $(MAKEDEPS)
	$(CC) -static $(INC) -o $@ $(OBJECTS) $(LIB) -lpthread -lutil -lm 

knot.pthreads : $(PTHREAD_OBJECTS) $(LIBUTIL) $(HEADERS) $(MAKEDEPS)
	$(CC) -I$(UTIL) -D_REENTRANT -o $@ $(PTHREAD_OBJECTS) -L$(UTIL) $(NEWLIBC) -lpthread -lutil


knot.pth : $(PTH_OBJECTS) $(LIBUTIL) $(HEADERS) $(MAKEDEPS)
	$(CC) -static -D_REENTRANT -I$(UTIL) -o $@ $(PTH_OBJECTS) -L$(UTIL) -lutil -lpth


test: knot
	./knot 8080 1000 ./html

testpthreads: knot.pthreads
	./knot.pthreads 8080 1000 ./html


SILENT = -s

$(LIBUTIL): FORCE
	@$(MAKE) $(SILENT) -C $(UTIL)

$(LIBAIO): FORCE
	@$(MAKE) $(SILENT) -C $(AIO)

$(LIBTHREAD): FORCE
	@$(MAKE) $(SILENT) -C $(THREAD)


FORCE:






