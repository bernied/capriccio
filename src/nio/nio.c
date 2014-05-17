/**
 * eepoll version of blocking IO routines
 *
 * These routines make use of asynchronous IO and the signal/wait
 * calls of a threading package to provide a blocking IO abstraction
 * on top of underlying non-blocking IO calls.
 *
 * The semantics of these calls mirror those of the standard IO
 * libraries.
 **/

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <libaio.h>
#include <sys/epoll.h>
#define __USE_GNU 1
//#define __USE_LARGEFILE64 1
#include <fcntl.h>
#include <linux/net.h>
#include <dirent.h>
#include "fdmap.h"
#include "threadlib_internal.h"

/*
 * Things to back-port to aio:
 * o lseek() handling (and opendir() handling)
 * o new poll() impl.
 * o send/sendto/sendmsg/recv/recvfrom/recvmsg wrappers
 */

/**
 * TODO: 
 *
 * o Signal-related syscalls.
 * 
 * o Kernel NULL pointer at aio_peak_evt()+0x17/0x85, when running testall
 * 
 * o APR test testproc is causing segfaults.
 * 
 * o File I/O is extremely slow with AIO.
 *
 * o Cancellable syscalls.
 * 
 * o (fixed) send/sendto/sendmsg/recv/recvfrom/recvmsg wrappers
 *   these are problematic because after a successful opr, we don't know the
 *   readiness of the descriptor anymore
 *
 * o (invalid) APR test, testproc.c:189 is failing because AIO refuses to accept
 * an iocb.  Seems it's because the fd was closed in a terminated child process.
 */

#ifndef DEBUG_nio_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif

// Global data
// AIO context
static io_context_t ioctx;
static int epfd;

#ifdef AIO_BATCH_REQUESTS
// Queue for batch AIO request submission
#define IOQ_DEPTH_MAX 32
int ioq_depth=0;
static struct iocb *ioq[IOQ_DEPTH_MAX];
#endif

// Use GCC magic to do initialization before main() is executed
// see: http://linux4u.jinr.ru/usoft/WWW/www_debian.org/Documentation/elf/node11.html
static void blocking_io_init () __attribute__ ((constructor));

// peak fd operation readiness (EPOLLIN, EPOLLOUT ...)
// any bit is 1 if the fd is *possibly* ready for that operation
// 0 if *not* ready
#define fd_status(fds) ((fds)->status)
// modify fd status to indicate the specified events are *not* ready
#define FD_NOT_READY(fds, events) ((fds)->status=(fds)->status & ~(events))

// wait for the fd to become ready
static inline int wait_for_fd (int fd, fdstruct_t * fds, iorequest_t *req, int events);
// wait for a aio operation to complete
static inline int submit_and_wait_for_iocb (struct iocb *iocb);

static inline int 
init_fdstruct(int fd, fdstruct_t *fds, int fdtype) {
	struct epoll_event evt;
	evt.events=EPOLLIN|EPOLLOUT|EPOLLPRI|EPOLLET;
	evt.data.ptr=fds;
	(fds)->type = fdtype;
	(fds)->flags = syscall(SYS_fcntl, (fd), F_GETFL); 
	return epoll_ctl(epfd, EPOLL_CTL_ADD, (fd), &evt); 
}


// from glibc source: include/libc-symbols.h
/*
# define strong_alias(name, aliasname) _strong_alias(name, aliasname)
# define _strong_alias(name, aliasname) \
  extern __typeof (name) aliasname __attribute__ ((alias (#name)));
*/

// macros for the i/o wrappers
#define CHECK_FD \
	fdstruct_t *fds; \
	int rt = 0; \
	CAP_SET_SYSCALL(); \
	fds = get_fdstruct(fd);	\
	if (fds == NULL) { errno = EBADF; return -1; }
#define GET_IOCB \
	struct iocb *iocb; \
	iocb = &current_thread->iocb;
#define DO_AIO(move_off) { \
      rt = wait_for_iocb(iocb); \
      if (move_off && rt > 0) \
        fds->off += rt; \
      rt = current_thread->ioret; \
    }
#define IS_NONBLOCK(fds) \
	((fds)->flags & O_NONBLOCK)

enum
{
    RW_READ,
    RW_WRITE,
    RW_PREAD,
    RW_PWRITE
};

#ifndef SYS_pread
#define SYS_pread         180
#define SYS_pwrite	181
#endif

#define RW_SYSCALL(type, fd, buf, count, off) \
	switch (type) {\
		case RW_READ: \
		rt = syscall(SYS_read, fd, buf, count); break; \
		case RW_WRITE: \
		rt = syscall(SYS_write, fd, buf, count); break; \
		case RW_PREAD: \
		 rt = syscall(SYS_pread, fd, buf, count, off); break; \
		case RW_PWRITE: \
		 rt = syscall(SYS_pwrite, fd, buf, count, off); break; } \
    if ((rt == -1 && errno == EAGAIN) || (rt >= 0 && rt < (int)count)) \
		switch (type) { \
			case RW_READ: case RW_PREAD: \
		        FD_NOT_READY (fds, EPOLLIN); break; \
		    case RW_WRITE: case RW_PWRITE: \
		    	FD_NOT_READY (fds, EPOLLOUT); break; }
// FIXME: I don't know whether the last condition of the 'if' above is correct		       



// steps to do a blocking i/o operation
// for network i/o
// 1. make sure the fd is in the eepoll fd list
// 2. block until the fd becomes ready or error
// 3. do the i/o
// for disk i/o
// 1. issue an aio request
// 2. block until the request finishes

static inline ssize_t
do_rw (int type, int fd, void *buf, size_t count, off_t off)
{
	iorequest_t req;
    CHECK_FD;
    INIT_IOREQUEST(&req);
    if (fds->type == FD_SOCK) {
    	short flag;
        // network i/o
        if (IS_NONBLOCK (fds)) {
            RW_SYSCALL (type, fd, buf, count, off);
            goto done;
        }
        flag = (type == RW_READ || type == RW_PREAD) ? EPOLLIN : EPOLLOUT;
        if (fd_status (fds) & flag) {
            RW_SYSCALL (type, fd, buf, count, off);
            if (rt >= 0 || errno != EAGAIN)
                goto done;
        }
        rt = wait_for_fd (fd, fds, &req, flag);
        if (rt == 0) {
            RW_SYSCALL (type, fd, buf, count, off);
        }
    } else {
        // disk i/o
        GET_IOCB;
        if (type == RW_READ || type == RW_WRITE)
            off = fds->off;
        switch (type) {
        case RW_READ:
        case RW_PREAD:
            io_prep_pread (iocb, fd, buf, count, off);
            break;
        case RW_WRITE:
        case RW_PWRITE:
            io_prep_pwrite (iocb, fd, buf, count, off);
            break;
        }
	iocb->data = current_thread;
        rt = submit_and_wait_for_iocb (iocb);
        if ((type == RW_READ || type == RW_WRITE)
             && rt > 0)
            fds->off += rt;
    }

  done:
    tdebug ("=%d, errno=%d\n", rt, errno);
    CAP_CLEAR_SYSCALL();
    return rt;
}

inline ssize_t
read (int fd, void *buf, size_t count)
{
    CAP_SET_SYSCALL();
    tdebug ("fd=%d  buf=%p  count=%d\n", fd, buf, count);
    return do_rw (RW_READ, fd, buf, count, 0);
}

strong_alias (read, __read);

inline ssize_t
write (int fd, const void *buf, size_t count)
{
    CAP_SET_SYSCALL();
    tdebug ("fd=%d  buf=%p  count=%d\n", fd, buf, count);
    return do_rw (RW_WRITE, fd, (void *)buf, count, 0);
}

strong_alias (write, __write);

inline ssize_t
pread (int fd, void *buf, size_t count, off_t off)
{
    CAP_SET_SYSCALL();
    tdebug ("fd=%d  buf=%p  count=%d  off=%lud\n", fd, buf, count, off);
    return do_rw (RW_PREAD, fd, buf, count, off);
}

strong_alias (pread, __pread);

inline ssize_t
pwrite (int fd, const void *buf, size_t count, off_t off)
{
    CAP_SET_SYSCALL();
    tdebug ("fd=%d  buf=%p  count=%d  off=%lud\n", fd, buf, count, off);
    return do_rw (RW_PWRITE, fd, (void *)buf, count, off);
}

strong_alias (pwrite, __pwrite);

// FIXME:
// inline ssize_t readv(int fd, const struct iovec *vector, int count)

// FIXME
// inline ssize_t writev(int fd, const struct iovec *vector, int count)

/**
 * Use SYS_socketcall to do a socket operation
 * socketcall: type of socketcall
 * flag: fd flag to wait for (EPOLLIN or EPOLLOUT)
 * args: pointer to first of all args passed to the socketcall
 */
static inline int
do_sock (int socketcall, int flag, int fd, void *args)
{
	iorequest_t req;
	CHECK_FD;
	INIT_IOREQUEST(&req);
    if (IS_NONBLOCK (fds)) {
		rt = syscall(SYS_socketcall, socketcall, args);
		goto done;
    }
    if (fd_status(fds) & flag) {
    	rt = syscall(SYS_socketcall, socketcall, args);
    	if (rt >= 0 || errno != EAGAIN)
    		goto done;
    }
	FD_NOT_READY (fds, flag);
    rt = wait_for_fd (fd, fds, &req, flag);
	if (rt == 0)
	    rt = syscall (SYS_socketcall, socketcall, args);
  done:
    tdebug("=%d\n", rt);
    CAP_CLEAR_SYSCALL();
    return rt;
}

int 
recv (int s, void *buf, size_t len, int flags) 
{
	(void)buf; (void)len; (void)flags;
	CAP_SET_SYSCALL();
	tdebug ("sockfd=%d, buf=%p, len=%d, flags=%d\n", s, buf, len, flags);
	return do_sock (SYS_RECV, EPOLLIN, s, &s);
}

int  
recvfrom (int  s, void *buf, size_t len, int flags, struct sockaddr
       * from, socklen_t *fromlen)
{
	(void)buf; (void)len; (void)flags; (void)from; (void)fromlen;
	CAP_SET_SYSCALL();
	tdebug ("sockfd=%d, buf=%p, len=%d, flags=%d, from=%p, fromlen=%d\n",
		s, buf, len, flags, from, *fromlen);
	return do_sock (SYS_RECVFROM, EPOLLIN, s, &s);
}

int
recvmsg(int s, struct msghdr *msg, int flags)
{
	(void)msg; (void)flags;
	CAP_SET_SYSCALL();
	tdebug ("sockfd=%d, msg=%p, flags=%d\n", s, msg, flags);
	return do_sock (SYS_RECVMSG, EPOLLIN, s, &s);
}

int 
send (int s, const void *msg, size_t len, int flags)
{
	(void)msg; (void)len; (void)flags;
	CAP_SET_SYSCALL();
	tdebug ("sockfd=%d, msg=%p, len=%d, flags=%d\n", s, msg, len, flags);
	return do_sock (SYS_SEND, EPOLLOUT, s, &s);
}

int  
sendto (int s, const void *msg, size_t len, int flags, const struct
       sockaddr *to, socklen_t tolen)
{
	(void)msg; (void)len; (void)flags; (void)to; (void)tolen;
	CAP_SET_SYSCALL();
	tdebug ("sockfd=%d, msg=%p, len=%d, flags=%d, to=%p, tolen=%d\n",
		s, msg, len, flags, to, tolen);
	return do_sock (SYS_SENDTO, EPOLLOUT, s, &s);
}

int 
sendmsg (int s, const struct msghdr *msg, int flags)
{
	(void)msg; (void)flags;
	CAP_SET_SYSCALL();
	tdebug ("sockfd=%d, msg=%p, flags=%d\n", s, msg, flags);
	return do_sock (SYS_SENDMSG, EPOLLOUT, s, &s);
}


int
open (const char *pathname, int flags, ...)
{
    mode_t mode;
    int fd;
    va_list ap;
    fdstruct_t *fds;

    CAP_SET_SYSCALL();
    tdebug ("pathname=%s  flags=%x\n", pathname, flags);
    if (flags & O_CREAT) {
        va_start (ap, flags);
        mode = va_arg (ap, mode_t);
        va_end (ap);
    } else {
        mode = 0x744;           // ignored later
    }

    fd = syscall (SYS_open, pathname, flags, mode);
    if (fd != -1) {
        fds = get_fdstruct (fd);
        assert (fds == NULL);   // the os should return a new fd
        fds = new_fdstruct (fd);
        init_fdstruct (fd, fds, FD_DISK);
    }   
    tdebug ("=%d\n", fd);
    CAP_CLEAR_SYSCALL();
    return fd;
}
strong_alias (open, __open);

/*
 // this doesn't work
 // it seems that opendir uses __open64()
 // but it cannot be intercepted in this way
int
open64 (const char *pathname, int flags, ...) 
{
    mode_t mode;
    va_list ap;

    tdebug ("pathname=%s  flags=%x\n", pathname, flags);
    if (flags & O_CREAT) {
        va_start (ap, flags);
        mode = va_arg (ap, mode_t);
        va_end (ap);
    } else {
        mode = 0x744;           // ignored later
    }
	return open (pathname, flags | O_LARGEFILE, mode);
}
strong_alias (open64, __open64);
*/

inline int
creat (const char *pathname, mode_t mode)
{
    CAP_SET_SYSCALL();
    debug ("path=%s\n", pathname);
    return open (pathname, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

strong_alias (creat, __creat);

inline int
close (int fd)
{
    fdstruct_t *fds;
    int rt;

    CAP_SET_SYSCALL();
    tdebug ("fd=%d\n", fd);
    fds = get_fdstruct (fd);
    if (fds != NULL)
		// this will decrease ref count of the fds and
	    // possibly delete it    
        remove_fdstruct (fd);   
    rt = syscall (SYS_close, fd);
    tdebug ("=%d\n", rt);
    CAP_CLEAR_SYSCALL();
    return rt;
}

strong_alias (close, __close);

inline off_t
lseek (int fd, off_t off, int whence)
{
    fdstruct_t *fds;
    off_t ret;

    CAP_SET_SYSCALL();
    tdebug ("fd=%d, off=%ld, whence=%d\n", fd, off, whence);
    fds = get_fdstruct (fd);

	// the only case we know so far that can result in fds being NULL
	// when fd is valid, is the fd is a directory fd
	// because we do not intercept opendir() and opendir doesn't call open()
	if (fds == NULL) {
		tdebug("no fds for fd=%d! assuming directory fd.\n", fd);
	    ret = syscall (SYS_lseek, fd, off, whence);
		goto done;
	}

    switch(whence) {
    case SEEK_SET: fds->off = off; ret = fds->off; break;
    case SEEK_CUR: fds->off += off; ret = fds->off; break;
    case SEEK_END:  {
    	ret = syscall(SYS_lseek, fd, off, whence);
		tdebug("seeking from end, ret=%ld\n", ret);
    	if (ret != -1)
    		fds->off = ret;
    	break;
    }
    default: ret = (off_t)-1; errno = EINVAL;
    }
	
	ret = fds->off;
  done:
    tdebug ("=%ld\n", ret);
    CAP_CLEAR_SYSCALL();
    return ret;
}

strong_alias (lseek, __lseek);

#define CHANGABLE_FFLAGS (O_APPEND | O_NONBLOCK | O_ASYNC | O_DIRECT)
#define NONCHANGABLE_FFLAGS (~CHANGABLE_FFLAGS)

int
fcntl (int fd, int cmd, ...)
{
    va_list ap;
    int ret;
    fdstruct_t *fds;

    CAP_SET_SYSCALL();
    fds = get_fdstruct (fd);
    va_start (ap, cmd);

    tdebug ("fd=%d, cmd=%x\n", fd, cmd);
    switch (cmd) {
    case F_DUPFD:
        {
            long newfd = va_arg (ap, long);
            ret = syscall (SYS_fcntl, fd, cmd, newfd);
            if (ret >= 0)
                dup_fdstruct (fds, newfd);
            break;
        }
    case F_SETFL:
        {
            long arg;
            long actual_arg;
            arg = va_arg (ap, long);
            tdebug ("F_SETFL: arg=%ld\n", arg);
            if (fds->type == FD_SOCK)
                // always nonblocking for sock fd
                actual_arg = arg | O_NONBLOCK;
            else
                actual_arg = arg;
            ret = syscall (SYS_fcntl, fd, cmd, actual_arg);
            if (ret == 0) {
                fds->flags = (fds->flags & NONCHANGABLE_FFLAGS)
                    | (arg & CHANGABLE_FFLAGS);
            }
            if (arg & O_NONBLOCK) {
            	tdebug ("setting fd=%d to O_NONBLOCK\n", fd);
            } else {
            	tdebug ("setting fd=%d to blocking\n", fd);
            }
            break;
        }
    case F_GETFL:
        {
            long flags;
            ret = fds->flags;
            // FIXME: this is only for debug
            flags = syscall (SYS_fcntl, fd, cmd);
            // make sure we have the right flags
            assert ((flags | O_NONBLOCK) == (ret | O_NONBLOCK));
            break;
        }
        // w/o arg case
    case F_GETFD:
    case F_GETOWN:
        ret = syscall (SYS_fcntl, fd, cmd);
        break;
        // w/ arg case
    default:
        {
            long arg = va_arg (ap, long);
            ret = syscall (SYS_fcntl, fd, cmd, arg);
            break;
        }
        // FIXME: deal with F_GETLK, which will block!
    }
    va_end (ap);
    tdebug ("=%d\n", ret);
    CAP_CLEAR_SYSCALL();
    return ret;
}

strong_alias (fcntl, __fcntl);

int
connect (int fd, const struct sockaddr *serv_addr, socklen_t addrlen)
{
    int rtlen;
    iorequest_t req;
    CHECK_FD;
    INIT_IOREQUEST(&req);
    (void)addrlen; (void)serv_addr;
    tdebug ("fd=%d, serv_addr=%p, addrlen=%d\n", fd, serv_addr, addrlen);
    fds->type = FD_SOCK;
    if (IS_NONBLOCK (fds)) {
        rt = syscall (SYS_socketcall, SYS_CONNECT, &fd);
        goto done;
    }
    // set the actual fd to non_blocking
    if (syscall (SYS_fcntl, fd, F_SETFL, fds->flags | O_NONBLOCK) < 0) {
        rt = -1;
        goto done;
    }

    rt = syscall (SYS_socketcall, SYS_CONNECT, &fd);
    if (rt != -1 || errno != EINPROGRESS)
        // finishes immediately
        goto done;
    FD_NOT_READY(fds, EPOLLOUT);

    // block until the connection is done
    rt = wait_for_fd (fd, fds, &req, EPOLLOUT);
    if (rt < 0)
        goto done;

    // find out result of connection
    rtlen = sizeof (errno);
    getsockopt (fd, SOL_SOCKET, SO_ERROR, &errno, &rtlen);
    rt = errno ? -1 : 0;

  done:
    tdebug ("=%d\n", rt);
    CAP_CLEAR_SYSCALL();
    return rt;
}

strong_alias (connect, __connect);


int
accept (int fd, struct sockaddr *addr, socklen_t * addrlen)
{
    iorequest_t req;
    CHECK_FD;
    (void)addr;
    (void)addrlen;
    INIT_IOREQUEST(&req);
    tdebug ("fd=%d, addr=%p, addrlen=%d\n", fd, addr, *addrlen);
    fds->type = FD_SOCK;
    if (IS_NONBLOCK (fds)) {
        rt = syscall (SYS_socketcall, SYS_ACCEPT, &fd);
        goto newfd;
    }
	// FIXME: this should be done when the fd is created (socket(), socketpair())
    // make sure the underlying socket is nonblocking
    if (syscall (SYS_fcntl, fd, F_SETFL, fds->flags | O_NONBLOCK) < 0) {
        rt = -1;
        goto done;
    }
	// try to see if it's already ready
    rt = syscall (SYS_socketcall, SYS_ACCEPT, &fd);
    if (rt != -1 || errno != EAGAIN)
    	goto newfd;
	FD_NOT_READY(fds, EPOLLIN);
	
    // wait for the socket to become readable
    while (1) {
        rt = wait_for_fd (fd, fds, &req, EPOLLIN);
        if (rt < 0) {
            goto done;
        }
        rt = syscall(SYS_socketcall, SYS_ACCEPT, &fd);
        if (rt != -1 || errno != EAGAIN)
	        // this could happen is someone else just stole our data
            goto newfd;
    }
  newfd:
	if (rt != -1) {
		fdstruct_t *fds;
		fds = new_fdstruct(rt);
		init_fdstruct(rt, fds, FD_SOCK);
	}
    
  done:
    tdebug ("=%d\n", rt);
    CAP_CLEAR_SYSCALL();
    return rt;
}

strong_alias (accept, __accept);


int 
poll (struct pollfd *ufds, nfds_t nfds, int timeout)
{
	int i;
	fdstruct_t *fds;
	iorequest_t *reqs;
	int n_ready = 0;

	CAP_SET_SYSCALL();
    tdebug("nfds=%d, timeout=%d, ufds[0].fd=%d\n", (int)nfds, timeout, ufds[0].fd);
	// call system's poll with 0 timeout to check if any is already ready
	n_ready = syscall (SYS_poll, ufds, nfds, 0);
	if (n_ready != 0 || timeout == 0)		// ready or error or non-blocking poll
		goto done;
	
    // none ready, we need to go to sleep and wait for these fds
    
	// we use 0 to denote infinite wait
	// and we use microseconds
	timeout = timeout < 0 ? 0 : timeout * 1000;
		
	// nothing is ready now, so wait on these fd's
	reqs = malloc(nfds * sizeof(iorequest_t));
	if (!reqs) {
		output("nio: Cannot get memory for request list.\n");
		exit(-1);
	}
	bzero(reqs, nfds * sizeof(iorequest_t));
	for (i = 0; i < (int)nfds; i++) {
		reqs[i].thread = current_thread;
		fds = get_fdstruct(ufds[i].fd);
		assert (fds);
		FD_NOT_READY (fds, ufds[i].events);	// we now know the fd is not ready
		if (ufds[i].events & POLLIN) {
			IOQ_ENQUEUE(&fds->wq_in, &reqs[i].q_in);
		}
		if (ufds[i].events & POLLOUT) {
			IOQ_ENQUEUE(&fds->wq_out, &reqs[i].q_out);
		}
		if (ufds[i].events & POLLPRI) {
			IOQ_ENQUEUE(&fds->wq_pri, &reqs[i].q_pri);
		}
	}
	
	// wait for events to happen or timeout
	tdebug ("going to sleep, timeout=%d\n", timeout);
	thread_suspend_self (timeout);
	tdebug ("waken up!\n");
		
	// collect newly occured events
	// FIXME: better way is sth like a "bottom half" (callback) that runs within
	// the scheduler thread
	for (i = 0; i < (int)nfds; i++) {
		fds = get_fdstruct(ufds[i].fd);
		assert (fds);
		if ( (ufds[i].events & fds->status) 
		      || (fds->status & (POLLERR | POLLHUP)) ) {
			ufds[i].revents = fds->status;
			n_ready ++;
		} else
			ufds[i].revents = 0;
	}

	// remove all ioqueue entries
	for (i = 0; i < (int)nfds; i++) {
		IOQ_REMOVE(&reqs[i].q_in);
		IOQ_REMOVE(&reqs[i].q_out);
		IOQ_REMOVE(&reqs[i].q_pri);
	}	

	free(reqs);
	
  done:
    tdebug("=%d\n", n_ready);
    CAP_CLEAR_SYSCALL();
    return n_ready;
}
strong_alias (poll, __poll);


int 
select (int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
	int rt = 0;
	fd_set _readfds, _writefds, _exceptfds;
	struct timeval _timeout = {0,0};
	iorequest_t *reqs;
	int i,idx = 0, n_ready = 0, isready;
	fdstruct_t *fds;
	long long expiretime;

	(void) timeout;
	
	CAP_SET_SYSCALL();
	tdebug ("n=%d, timeout.sec=%ld, timeout.usec=%ld\n", n, timeout->tv_sec, timeout->tv_usec);
	
	// special case used by APR that doesn't do well with our emulation
	if (n == 0 && timeout) {
		rt = usleep (timeout->tv_sec * 1000000 + timeout->tv_usec);
		goto done;
	}
	
	// call select() syscall with 0 timeout to see if already ready
	if (readfds)
		memcpy (&_readfds, readfds, sizeof(fd_set));
	if (writefds) 
		memcpy (&_writefds, writefds, sizeof(fd_set));
	if (exceptfds)
		memcpy (&_exceptfds, exceptfds, sizeof(fd_set));
	if (timeout)
		memcpy (&_timeout, timeout, sizeof(struct timeval));
	rt = syscall (n, readfds, writefds, exceptfds, timeout);
	if (rt > 0 || rt == -1 || (timeout && _timeout.tv_sec == 0 && _timeout.tv_usec==0)) {
		goto done;
	}

	// nothing ready now, then we wait for these fds'
	// FIXME: this may be WAY too much (don't forget to fix the bzero() below!)
	reqs = malloc(n * sizeof(iorequest_t));
	if (!reqs) {
		output("nio: Cannot get memory for request list.\n");
		exit(-1);
	}
	bzero(reqs, n * sizeof(iorequest_t));
	
	// FIXME: we do not have a separate wait queue for exceptions
	// So now when select() is called with a exceptfd not included in readfds or
	// writefds, it will fail with ENOSYS.
retry:
	for (i = 0; i < (int)n; i++) {
		short events = 0;
		
		if (!FD_ISSET(i, &_readfds) && !FD_ISSET(i, &_writefds) && !FD_ISSET(i, &_exceptfds))
			continue;
		if (!FD_ISSET(i, &_readfds) && !FD_ISSET(i, &_writefds) && FD_ISSET(i, &_exceptfds)) {
			output ("BUG: select only for exceptions is not implemented!");
			rt = -1; errno = ENOSYS;
			goto done;
		}
		reqs[idx].thread = current_thread;
		fds = get_fdstruct(i);
		assert (fds);	// because first select() syscall above did not fail
		if (FD_ISSET(i, &_readfds)) {
			IOQ_ENQUEUE(&fds->wq_in, &reqs[idx].q_in);
			events |= POLLIN;
		}
		if (FD_ISSET(i, &_writefds)) {
			IOQ_ENQUEUE(&fds->wq_out, &reqs[i].q_out);
			events |= POLLOUT;
		}
		FD_NOT_READY (fds, events);	// we now know the fd is not ready
		idx ++;
	}
	
	// wait for events to happen or timeout
	if (!timeout) 
		expiretime = 0;		// infinite time
	else
		expiretime = _timeout.tv_usec + _timeout.tv_sec*1000000;
	tdebug ("Going to sleep for %lld us\n", expiretime);
	thread_suspend_self (expiretime);
	tdebug ("Wake up\n");
		
	// collect newly occured events
	for (i = 0; i < n; i++) {
		if (!FD_ISSET(i, &_readfds) && !FD_ISSET(i, &_writefds))
			continue;

		isready = 0;
		fds = get_fdstruct(i);
		assert (fds);
		if (FD_ISSET(i, &_readfds) && (fds->status & POLLIN) ) {
			FD_SET(i, readfds);
			isready = 1;
		}
		if (FD_ISSET(i, &_writefds) && (fds->status & POLLOUT)) {
			FD_SET(i, writefds);
			isready = 1;
		}
		if (FD_ISSET(i, &_exceptfds) && (fds->status & (POLLHUP | POLLERR))) {
			FD_SET(i, exceptfds);
			isready = 1;
		}
		
		n_ready += isready;
	}

	// remove all ioqueue entries
	for (i = 0; i < idx; i++) {
		IOQ_REMOVE(&reqs[i].q_in);
		IOQ_REMOVE(&reqs[i].q_out);
		IOQ_REMOVE(&reqs[i].q_pri);
	}	
	
	if (n_ready == 0) {
		toutput ("nio: (WARNING) False wakeup in select() because of exceptfds!\n");
		goto retry;
	}

done:
	tdebug ("=%d\n", rt);
	return rt;
}
strong_alias (select, __select);

int
pselect (int   n,   fd_set   *readfds,  fd_set  *writefds,  fd_set
       *exceptfds, const struct timespec *timeout, const sigset_t *sigmask)
{
	(void)n; (void)readfds; (void)writefds; (void)exceptfds; (void)timeout; (void)sigmask;
	tdebug ("pselect not implemented!!!\n");
	errno = ENOSYS;
	return -1; // pselect not implemented
}
strong_alias (pselect, __pselect);

       	

int
dup (int fd)
{
    int newfd;
    CAP_SET_SYSCALL();
    tdebug ("fd=%d\n", fd);
    newfd = syscall (SYS_dup, fd);
    if (newfd != -1) {
        fdstruct_t *fds = get_fdstruct (fd);
        dup_fdstruct (fds, newfd);
    }
    tdebug ("=%d\n", newfd);
    CAP_CLEAR_SYSCALL();
    return newfd;
}

strong_alias (dup, __dup);

int
dup2 (int oldfd, int newfd)
{
    int ret;
    CAP_SET_SYSCALL();
    tdebug ("oldfd=%d, newfd=%d\n", oldfd, newfd);
    ret = syscall (SYS_dup2, oldfd, newfd);
    if (ret != -1) {
        fdstruct_t *fds = get_fdstruct (oldfd);
        if (get_fdstruct (newfd) != NULL)	// because dup2 closes existing file on newfd
        	remove_fdstruct(newfd);
        dup_fdstruct (fds, newfd);
    }
    tdebug ("=%d\n", ret);
    CAP_CLEAR_SYSCALL();
    return ret;
}

strong_alias (dup2, __dup2);

/*
// this doesn't work
// __opendir is not exported
extern DIR * __opendir(const char *name);

DIR *
opendir (const char *name) 
{
	DIR *rt;
	tdebug ("name=%s\n", name);
	rt = __opendir(name);
	if (rt) {
		int fd;
		fdstruct_t *fds;
		fd = dirfd(rt);
		fds = new_fdstruct(fd);
		init_fdstruct(fd, fds, FD_DISK);
	}
	tdebug ("=%p\n", rt);
	return rt;
}
*/

// FIXME: what about signals?
unsigned int
sleep (unsigned int sec)
{
    CAP_SET_SYSCALL ();
    thread_usleep ((unsigned long long) sec * 1000000);
    CAP_CLEAR_SYSCALL ();
    return 0;
}

strong_alias (sleep, __sleep);

// FIXME: what about signals?
int
usleep (__useconds_t usec)
{
    CAP_SET_SYSCALL();
    tdebug ("usec=%ld\n", (long) usec);
    thread_usleep (usec);
    CAP_CLEAR_SYSCALL();
    return 0;
}

strong_alias (usleep, __usleep);


int
pipe (int filedes[2])
{
    int ret;
    CAP_SET_SYSCALL(); 
    ret = syscall (SYS_pipe, filedes);
    tdebug ("filedes[]={%d, %d}\n", filedes[0], filedes[1]);
    if (ret == 0) {
        fdstruct_t *fds;
        fds = new_fdstruct (filedes[0]);
		init_fdstruct(filedes[0], fds, FD_SOCK);
		// set nonblocking option on actual fds
	    if (syscall (SYS_fcntl, filedes[0], F_SETFL, fds->flags | O_NONBLOCK) < 0) {
    	    ret = -1;
        	goto done;
	    }

        fds = new_fdstruct (filedes[1]);
		init_fdstruct(filedes[1], fds, FD_SOCK);
	    if (syscall (SYS_fcntl, filedes[1], F_SETFL, fds->flags | O_NONBLOCK) < 0) {
    	    ret = -1;
        	goto done;
	    }
    }
done:
    tdebug ("=%d\n", ret);
    CAP_CLEAR_SYSCALL();
    return ret;
}

strong_alias (pipe, __pipe);


int
socketpair (int d, int type, int protocol, int sv[2])
{
    int ret;
    (void) type;
    (void) protocol;
    CAP_SET_SYSCALL();
    tdebug ("d=%d\n", d);

    ret = syscall (SYS_socketcall, SYS_SOCKETPAIR, &d);
    if (ret == 0) {
        fdstruct_t *fds;
        fds = new_fdstruct (sv[0]);
		init_fdstruct(sv[0], fds, FD_SOCK);
        fds = new_fdstruct (sv[1]);
		init_fdstruct(sv[1], fds, FD_SOCK);
    }
    tdebug ("=%d\n", ret);
    CAP_CLEAR_SYSCALL();
    return ret;
}

strong_alias (socketpair, __socketpair);


int
socket (int domain, int type, int protocol)
{
    int ret;
    (void)type;
    (void)protocol;
    CAP_SET_SYSCALL();
    tdebug ("domain=%d, type=%d, protocol=%d\n", domain, type, protocol);
    // FIXME: is this correct?
    ret = syscall (SYS_socketcall, SYS_SOCKET, &domain);
    if (ret != -1) {
        fdstruct_t *fds;
        fds = new_fdstruct (ret);
		if (init_fdstruct(ret, fds, FD_SOCK) == -1) {
			perror("cannot add socket fd to epoll interest list.");
			ret = -1;
		}
    }
    tdebug ("=%d\n", ret);
    CAP_CLEAR_SYSCALL();
    return ret;
}

//
// Internal functions
//

static struct epoll_event _events[1024];
static struct io_event ioevts[8192];

int __nio_epoll_wait_count = 0;
int __nio_epoll_wait_return_count = 0;
int __nio_io_getevents_count = 0;
int __nio_io_getevents_return_count = 0;
int __nio_io_getevents_fast_count = 0;
int __nio_io_getevents_fast_return_count = 0;

// main event loop: get i/o events and resume waiting threads
// block: whether we should block waiting for events
// timeout: timeout in microsecs
static void
blocking_io_poll (long long timeout)
{
    int i, count;
    int time;
    tdebug ("timeout=%lld\n", timeout);
	if (timeout > 0)
		time = timeout / 1000 + 1;
	else if (timeout == 0)
		time = 0;
	else
		time = -1;
#ifdef AIO_BATCH_REQUESTS
    // submit all outstanding AIO requests
    if (ioq_depth > 0) {
        if (io_submit(ioctx, ioq_depth, ioq) != ioq_depth) {
            // FIXME: wake up those threads
	    fatal("io_submit failed\n");
	}
	ioq_depth = 0;
    }
#endif
	
    while ( (count = epoll_wait (epfd, _events, 1024, time)) == -1
    	&& errno == EINTR) {};
    __nio_epoll_wait_count++;
    __nio_epoll_wait_return_count += count;
    
    for (i = 0; i < count; i++) {
        if (_events[i].events & 0x2000) {
			// FIXME: we should loop to get all events if there are more than 100
            // AIO event
            struct timespec ts = {.tv_sec = 0,.tv_nsec = 0 };
            int cnt, j;
            cnt = io_getevents (ioctx, 1, 1024, ioevts, &ts);
	    __nio_io_getevents_count++;
	    __nio_io_getevents_return_count+=cnt;
            for (j = 0; j < cnt; j++) {
                thread_t *thread = (thread_t *) ioevts[j].data;
                thread->ioret = ioevts[j].res;
                thread_resume (thread);
            }
        } else {
            fdstruct_t *fds = (fdstruct_t *) _events[i].data.ptr;
            short events;
            events = _events[i].events;
            fds->status |= events;
            if (events & EPOLLIN) {
                ioqueue_t *q;
                // FIXME: waking up only one waiter will cause trouble if
                // the one waken up only consumes part of the buffer
				IOQ_DEQUEUE (&fds->wq_in, q);
				if (q)
                	thread_resume (IOQ_GET_REQUEST(q, q_in)->thread);
            }
            if (events & EPOLLOUT) {
				ioqueue_t *q;
                // FIXME: waking up only one waiter will cause trouble if
                // the one waken up only consumes part of the buffer
				IOQ_DEQUEUE (&fds->wq_out, q);
				if (q)
					thread_resume (IOQ_GET_REQUEST(q, q_out)->thread);
            }
            if (events & EPOLLPRI) {
            	ioqueue_t *q;
            	IOQ_DEQUEUE (&fds->wq_pri, q);
            	if (q)
            		thread_resume (IOQ_GET_REQUEST(q, q_pri)->thread);
            }
            if ((events & EPOLLERR)
                 || (events & EPOLLHUP)) {
                // wake up all waiting threads in case of error
                ioqueue_t *q;
                while (1) {
                	IOQ_DEQUEUE (&fds->wq_in, q);
                	if (!q)
                		break;
                	thread_resume (IOQ_GET_REQUEST(q, q_in)->thread);
                }
                while (1) {
                	IOQ_DEQUEUE (&fds->wq_out, q);
                	if (!q)
                		break;
                	thread_resume (IOQ_GET_REQUEST(q, q_out)->thread);
                }
                while (1) {
                	IOQ_DEQUEUE (&fds->wq_pri, q);
                	if (!q)
                		break;
                	thread_resume (IOQ_GET_REQUEST(q, q_pri)->thread);
                }
            }

        }
    }
}

static void *foo;	// used only to ensure proper initialization, see below

static void
blocking_io_init ()
{
    // init fdstruct for standard i/o files
    fdstruct_t *fd0, *fd1, *fd2;
    struct epoll_event evt;

    tdebug ("blocking_io_init()\n");
    foo = (void *)current_thread;
//    thread_init ();

    // init epoll
    epfd = epoll_create (1024);
    if (epfd == -1) {
        perror
            ("Cannot initialize epoll.  Do you have epoll compiled into your kernel?");
        exit(-1);
    }

    fd0 = new_fdstruct (0);
    fd1 = new_fdstruct (1);
    fd2 = new_fdstruct (2);

	// these may well fail for various reasons (eg. 0 and 1 are probably the 
	// same file, so the second one will cause EEXIST).
	// we can safely ignore all errors
//    init_fdstruct (0, fd0, FD_SOCK);

	// FIXME: eliminate this special case for FD 0
	// this (only polling for EPOLLIN) exists because epoll_wait as of 2.5.70 
	// returns immediately saying fd 0 becomes ready for EPOLL_OUT each time 
	// called.
	{
		struct epoll_event evt;
		evt.events = EPOLLIN|EPOLLET;
		evt.data.ptr=fd0;
		fd0->type = FD_SOCK;
		fd0->flags = syscall(SYS_fcntl, 0, F_GETFL); 
		epoll_ctl(epfd, EPOLL_CTL_ADD, 0, &evt); 
	}
    init_fdstruct (1, fd1, FD_SOCK);
    init_fdstruct (2, fd2, FD_SOCK);

    set_io_polling_func (blocking_io_poll);

    // init aio context
    if (io_setup (10000, &ioctx) < 0) {
        perror
            ("Cannot initialize Linux AIO.  Do you have aio compiled into your kernel?");
        exit (-1);
    }
    evt.events = 0;
    if (epoll_ctl (epfd, EPOLL_CTL_ADD | 0x2000, (int) ioctx, &evt) < 0) {
        perror
            ("Cannot add AIO context into epoll.  Do you have eepoll by Feng compiled into your kernele?");
        exit (-1);
    }
    tdebug ("NIO initialized\n");
}



// wait for a fd to become ready
// pre: the fd must be marked not ready for _events_.  waiting for a already
//      ready fd will block forever
// return: 0 if successful, -1 if otherwise, errno set to EAGAIN if interrupted by signal
static inline int
wait_for_fd (int fd, fdstruct_t * fds, iorequest_t *req, int events)
{
    ioqueue_t *q, *wq;
    int rv;

    (void)fd;
    // we should not go to sleep if we know we are possibly ready
    // because it may never wake up again if the kernel has already delivered
    // the readiness event
    assert (!(fds->status & events));

    events |= POLLERR;
    assert ((events & EPOLLIN) || (events & EPOLLOUT));
    assert (!((events & EPOLLIN) && (events & EPOLLOUT)));
    if (events & EPOLLIN) {
    	wq = &fds->wq_in;
    	q = &req->q_in;
    } else {
    	wq = &fds->wq_out;
    	q = &req->q_out;
    }

    // add current thread to the wait queue
	IOQ_ENQUEUE(wq, q);

    // block util fd is ready
    tdebug ("going to sleep for fd: %d\n", fd);
    rv = thread_suspend_self (0);
    tdebug ("waken up\n");
    if (rv == INTERRUPTED) {
    	rv = -1;
    	errno = EAGAIN;
    }

	// just to make sure
	IOQ_REMOVE(q);
	
    return rv;
}

int __cap_outstanding_disk_requests = 0;

// submit and wait for a aio operation to complete
// return: result of I/O, just as a blocking read/write
static inline int
submit_and_wait_for_iocb (struct iocb *iocb)
{
    int rt;
#ifdef AIO_BATCH_REQUESTS
    ioq[ioq_depth++] = iocb;
    if (ioq_depth == IOQ_DEPTH_MAX) {
	rt = io_submit(ioctx, ioq_depth, ioq);
	if (rt != ioq_depth) {
		errno = -rt;
		rt = -1;
		// FIXME: wake up other threads too!
		fatal("io_submit failed\n");
		goto done;
	}
	ioq_depth = 0;
    }
 #else

    rt = io_submit(ioctx, 1, &iocb);
    if (rt != 1) {
	    errno = -rt;
	    rt = -1;
	    goto done;
    }
    
    // See if the operation is already done (probably a cache hit)
    // This should save 2 context switches and 2 wait queue manipulations
    // FIXME: we can use some kernel help here.  io_submit can
    // really return an indication whether it is already done.
    // or it can just return the event back if it is.
    {
	    struct timespec ts = {.tv_sec = 0,.tv_nsec = 0 };
            int cnt, j;
	    int thisdone = 0;
            cnt = io_getevents (ioctx, 1, 1024, ioevts, &ts);
	    __nio_io_getevents_fast_count++;
	    __nio_io_getevents_fast_return_count+=cnt;
            for (j = 0; j < cnt; j++) {
                thread_t *thread = (thread_t *) ioevts[j].data;
		if (thread == current_thread) {
			thisdone = 1;
			rt = ioevts[j].res;
		} else {
	                thread->ioret = ioevts[j].res;
        	        thread_resume (thread);
		}
            }
	    if (thisdone) {
		    if (current_thread->iocount++ >= 1024) {
			    current_thread->iocount = 0;
			    thread_yield();
		    }
		    goto done;
	    }
    }
 #endif

    // nothing needs to be done here
    // just go to sleep and the io main loop will wake us up when
    // the io completion event is seen
    tdebug ("going to sleep for iocb\n");
    __cap_outstanding_disk_requests++;
    thread_suspend_self (0);
    __cap_outstanding_disk_requests--;
    tdebug ("waken up\n");
    rt = current_thread->ioret;
done:
    return rt;
}
