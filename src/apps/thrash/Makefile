
include ../../Make.include

LIBCAP = $(SRCDIR)/lib/libpthread.a

INC = -I$(UTIL) 


HEADERS = $(wildcard *.h)

TARGETS = memfiller mpin

all: checklib $(TARGETS)

clean:
	rm -f $(TARGETS)

$(TARGETS) : % : %.c $(HEADERS) $(LIBCAP)
	$(CC) -static $(INC) -o $* $*.c $(LIBCAP)


p$(TARGET) : $(PTHREAD_OBJECTS) $(HEADERS)
	gcc -static $(INC) -o $@ $(PTHREAD_OBJECTS) $(LIBUTIL) -lpthread


checklib: FORCE
	@make -s -C $(AIO)


FORCE:






