
COUNTLINES_SKIP := ./apache  ./apps  ./threads/coro-1.1.0.pre2  ./util/old   \
     ./aio/libaio  ./aio/epoll-lib   *test.c   ./aio/tests 

# for HOTOS
HOTOS_COUNTLINES_SKIP := $(COUNTLINES_SKIP) ./threads/sched_graph* ./stack ./include/fptrcheck.h \
   ./aio/sockio_epoll.c ./aio/diskio_aio.c ./aio/diskio_blocking.c
#COUNTLINES_SKIP := $(HOTOS_COUNTLINES_SKIP)

include ./Make.include

ifeq '$(USE_IO_LIB)' 'nio'
SUBDIRS = util stack threads nio apps/knot apps/client
else
SUBDIRS = util stack threads aio apps/knot apps/client
endif

CLEANDIRS = util stack threads nio aio apps/knot apps/client 

all:
	@for d in $(SUBDIRS); do \
		echo ""; \
		echo "*** making in $$d ***"; \
		$(MAKE) -C $$d; \
		if [ $$? != 0 ]; then exit 1; fi; \
	done


clean:
	@for d in $(CLEANDIRS); do \
		echo ""; \
		echo "*** cleaning in $$d ***"; \
		$(MAKE) -C $$d clean; \
		if [ $$? != 0 ]; then exit 1; fi; \
	done


