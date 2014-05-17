
include ../../Make.include

LIBCAP = $(SRCDIR)/lib/libpthread.a

LT_INC = -I$(UTIL)
#LT_LIB = -L/opt/testing/lib -lpthread
LT_LIB = -lpthread
#LT_GCC = /opt/testing/bin/gcc
LT_GCC = $(CC)

LIB = -L$(SRCDIR)/lib $(LT_LIB)
INC = -I$(UTIL) -I$(SRCDIR)/include

SOURCES = $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)
PTHREAD_OBJECTS = $(SOURCES:.c=.po)
PTH_OBJECTS = $(SOURCES:.c=.ptho)
HEADERS = $(wildcard *.h)
PCFLAGS = -DPCLIENT


all: checklib client pclient mclient mpclient 

clean:
	rm -f client pclient *.o *.po

%.o: %.c $(HEADERS) $(MAKEDEPS)
	$(CC) $(CFLAGS) $(INC) -o $@ -c $<

#%.mo: %.c $(HEADERS) $(MAKEDEPS)
#	$(CC) $(CFLAGS) $(INC) -o $@ -c $<#

%.po: %.c $(HEADERS) $(MAKEDEPS)
	$(LT_GCC) $(PCFLAGS) $(LT_INC) -o $@ -c $<


client : $(OBJECTS) $(HEADERS) $(LIBCAP)
	$(CC) -static $(INC) -o $@ $(OBJECTS) $(LIB) -lm


# a capriccio client built w/ older libs, to run on millennium
mclient : $(OBJECTS) $(HEADERS) $(LIBCAP)
	$(CC) -static $(INC) -o $@ $(OBJECTS) -L$(SRCDIR)/lib -L$(UTIL) -lpthread -lutil -lm

mpclient : $(PTHREAD_OBJECTS) $(HEADERS)
	$(CC) -static -o $@ $(PTHREAD_OBJECTS) -L$(UTIL) -lutil -lpthread

pclient : $(PTHREAD_OBJECTS) $(HEADERS)
	$(LT_GCC) -static $(LT_INC) -o $@ $(PTHREAD_OBJECTS) -L$(UTIL) -lutil $(LT_LIB)


checklib: FORCE
	@make -s -C $(AIO)


FORCE:






