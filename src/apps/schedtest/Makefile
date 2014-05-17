
include ../../Make.include

INC = -I$(UTIL) -I$(THREAD)
LIB := -L. -L$(THREAD) -L$(UTIL) -lthread -lutil

CC := $(CC)

TESTPROGS = batchtest
OTHERPROGS = cachetest vmtest
#SCHEDULERS = global_rr graph_rr graph_batch graph_priority
SCHEDULERS = global_rr graph_batch graph_priority
OBJS = clearcache.o

all: $(OTHERPROGS) $(TESTPROGS)

%.o: %.c
	$(CC) -static $(INC) -c -o $@ $<

$(OTHERPROGS) $(TESTPROGS) : % : %.c $(OBJS) $(LIBTHREAD) $(LIBUTIL) Makefile $(MAKEOPTS)
	$(CC) -static $(INC) -o $@ $< $(OBJS) $(LIB)

test: $(TESTPROGS)
	./batchtest

timetests: $(TESTPROGS)
	@for t in $(TESTPROGS); do \
		echo "------------------------------------"; \
		echo "testing $$t"; \
		echo "------------------------------------"; \
		for s in $(SCHEDULERS); do \
			echo -n "scheduler=$$s"; \
			time CAPRICCIO_SCHEDULER=$$s ./$$t; \
			echo ""; \
		done; \
		echo ""; \
	done ;

# rule to check the utility library
$(LIBUTIL): FORCE
	@$(MAKE) -s -C $(UTIL)

# rule to check the thread library
$(LIBTHREAD): FORCE
	@$(MAKE) -s -C $(THREAD)

FORCE:

clean:
	rm -f *.o *.a *~ core* $(TESTPROGS) $(OTHERPROGS)









