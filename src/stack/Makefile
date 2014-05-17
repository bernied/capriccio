
include ../Make.include

INC=-I$(UTIL)
LIB=

LIBSTACK = libstack.a

CFILES := $(filter-out %test.c, $(wildcard *.c))
OBJS   := $(patsubst %.c, %.o, $(CFILES))
HEADERS := $(wildcard *.h)

all: $(LIBSTACK)

$(LIBSTACK): $(OBJS)
	$(AR) rc $@ $^

%.o: %.c $(HEADERS) $(MAKEDEPS)
	$(CC) $(CFLAGS) $(INC) -o $@ -c $<

clean:
	rm -f *.o *.a *~ core
