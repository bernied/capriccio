ARCH=/tmp/capriccio/src/threads/coro-1.1.0.pre2/arch/x86-linux

test: t
	./t >t.out
	diff -u t.out.ok t.out

t: t.c $(ARCH)/coro.h $(ARCH)/libcoro.a
	gcc -O -o t -I$(ARCH) -L$(ARCH) t.c -lcoro

clean:
	rm -f *.o core t t.out
