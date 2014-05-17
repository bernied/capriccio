/**
 * A map of information about all file descriptors
 */
#ifndef FDMAP_H
#define FDMAP_H

#include <sys/types.h>
#include "util.h"
#include "threadlib.h"
#include <linux/stddef.h>   // FIXME: for offsetof()

/**
 * container_of - cast a member of a structure out to the containing structure
 *
 * @ptr:        the pointer to the member.
 * @type:       the type of the container struct this is embedded in.
 * @member:     the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})

typedef struct ioqueue {
	struct ioqueue *next, *prev;
} ioqueue_t;

typedef struct iorequest {
	// point to the request itself
	thread_t *thread;
	ioqueue_t q_in, q_out, q_pri;
} iorequest_t;

#define INIT_IOREQUEST(req) { \
	bzero(req, sizeof(iorequest_t)); \
	(req)->thread = current_thread; }

// enqueue an entry (q) into an ioqueue (wq)
#define IOQ_ENQUEUE(wq, q) { \
	(q)->prev = (wq)->prev; \
	(q)->next = (wq); \
	(wq)->prev->next = (q); \
	(wq)->prev = (q); }

// remove the head entry of an ioqueue (wq) and put it in (q)
#define IOQ_DEQUEUE(wq, q) {\
	(q) = (wq)->next != (wq) ? (wq)->next : NULL; \
	if (q) { \
		(q)->next->prev = (wq); \
		(wq)->next = (q)->next; \
		(q)->next = (q)->prev = NULL; } }

// remove an ioqueue entry (q) from the queue
#define IOQ_REMOVE(q) {\
	if ((q)->prev) { \
		(q)->prev->next = (q)->next; \
		(q)->next->prev = (q)->prev; \
		(q)->prev = (q)->next = NULL; } }

// get the container iorequest struct from the queue entry pointer (q)
// the member name of the queue entry is _member_
#define IOQ_GET_REQUEST(q, member)	container_of(q, iorequest_t, member)

struct fdstruct {
  //  latch_t latch;
  int refcnt;
  enum {FD_DISK, FD_SOCK} type;
  off_t off;
  // whether it is nonblocking
  int flags;		// flags returned by F_GETFL
  // whether the fd is ready for read/write
  // if any bit is 1, then that operation is *possibly* ready
  // if 0, then it is not ready
  int status;
  // all threads waiting for input/output/... on this fd
  ioqueue_t wq_in, wq_out, wq_pri, wq_err;
};
typedef struct fdstruct fdstruct_t;

void init_fdmap();

// get the fdstruct mapped to by fd
fdstruct_t *get_fdstruct(int fd);

// create a new fdstruct and assign fd to it
fdstruct_t *new_fdstruct(int fd);

// assign newfd to the fds and increase the fds' use count
fdstruct_t *dup_fdstruct(fdstruct_t *fds, int newfd);

// remove the mapping between fd and its corresponding fdstruct
// decrease the use count of the fdstruct
// remove it when the count reaches 0
void remove_fdstruct(int fd);

#endif
