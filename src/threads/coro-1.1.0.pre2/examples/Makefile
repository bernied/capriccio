ARCH=/tmp/capriccio/src/threads/coro-1.1.0.pre2/arch/x86-linux
CFLAGS=-O -Wall -I$(ARCH)
LDFLAGS=-L$(ARCH)
LDLIBS=-lcoro

SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)
EXEC=$(SRCS:.c=)

all: $(EXEC)

$(OBJS): $(ARCH)/coro.h
$(EXEC): $(ARCH)/libcoro.a

clean:
	rm -f *.o core $(EXEC)
