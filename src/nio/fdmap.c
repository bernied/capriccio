/**
 * A map of information about all file descriptors
 */

#include <string.h>
#include <sys/epoll.h>
#include "fdmap.h"
#include "util.h"

// fdstructs pointers are allocated in banks
#define FD_BANK_SHIFT 15
#define FDS_PER_BANK (1<<FD_BANK_SHIFT)  // 2^15
#define FD_BANK_MASK (FDS_PER_BANK - 1)  // 2^15 - 1

// allow up to 1 million  
#define MAX_FD (1024*1024)
#define MAX_BANK ((MAX_FD >> FD_BANK_SHIFT) + 1)

fdstruct_t **fdstruct_list[MAX_BANK];

int next_new_fd = 0;

//latch_t fdstruct_latch;

void init_fdmap() {
  // nothing
}

fdstruct_t *get_fdstruct(int fd) {
  fdstruct_t **bank = fdstruct_list[fd >> FD_BANK_SHIFT];
  if (bank == NULL)
    return NULL;
  return bank[fd & FD_BANK_MASK];
}

// internal function to assign a fd to a fds
// allocates banks if necessary
// checks whether the fd is already assigned to another fds
static inline void assign_fdstruct(int fd, fdstruct_t *fds) {
  fdstruct_t **bank;

  assert (get_fdstruct(fd) == NULL);
  assert (fds != NULL);

  bank = fdstruct_list[fd >> FD_BANK_SHIFT];
  if (bank == NULL)
    bank = fdstruct_list[fd >> FD_BANK_SHIFT] = 
      (fdstruct_t **)malloc(sizeof(fdstruct_t *)*FDS_PER_BANK);
  assert(bank != NULL);
  bank[fd & FD_BANK_MASK] = fds;
}

fdstruct_t *new_fdstruct(int fd) {
  fdstruct_t *fds;

  fds = (fdstruct_t *)malloc(sizeof(fdstruct_t));
  assert(fds != NULL);

  // initialize the fdstruct_t
  bzero(fds, sizeof(fdstruct_t));
  fds->refcnt = 1;
  fds->status = EPOLLOUT;		// we default a new socket to be ready to write only
  fds->wq_in.next = fds->wq_in.prev = &fds->wq_in;
  fds->wq_out.next = fds->wq_out.prev = &fds->wq_out;
  fds->wq_pri.next = fds->wq_pri.prev = &fds->wq_pri;
  assign_fdstruct(fd, fds);
  return fds;
}

fdstruct_t *dup_fdstruct(fdstruct_t *fds, int newfd) {
  fds->refcnt++;
  assign_fdstruct(newfd, fds);
  return fds;
}

void remove_fdstruct(int fd) {
  fdstruct_t **bank = fdstruct_list[fd >> FD_BANK_SHIFT];
  fdstruct_t *fds;
  assert (bank);
  fds = bank[fd & FD_BANK_MASK];
  assert(fds);
  assert(fds->refcnt > 0);
  bank[fd & FD_BANK_MASK] = NULL;
  fds->refcnt--;
  if (fds->refcnt == 0) {
    free(fds);
  }
  // FIXME: better free the bank if it contains no data any more
}
