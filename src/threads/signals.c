
/**
 * POSIX thread signal implementation.  Both external (inter-process) signals and internal
 * (inter-thread) signals using thread_kill (pthread_kill) are implemented.
 * 
 * FIXME:
 * o Essentially all places using thread_suspend_self() should be updated to deal with signals.
 *   one way would be to add an argument to thread_suspend_self() to indicate whether it should
 *   return after interrupted by a signal (for most places, it is a NO).
 * 
 * o The current data structure (sig_received & sig_num_received) results in very slow fast path
 *   when there is a blocked signal pending.  A better way is to use a bitmap for more than one
 *   pending signals and a single variable when there's only one.
 * 
 * o sig_process_signal() should not look at the global 'sig_received' bitmap, because anything
 *   there is now blocked by all threads.  We should check it only when,
 *     1). Signal mask of current thread is changed
 *     2). New threads are created
 * 
 * o Signal-within-signal races may exist.
 * 
 * o Nothing is SMP-safe now.
 */

#include "threadlib_internal.h"
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <syscall.h>
#include <sys/syscall.h>
#include <linux/compiler.h>
#include "config.h"

#ifndef DEBUG_signals_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif

#define IGNORE 1
#define TERMINATE 2

typedef void (*sighandler_t) (int);
typedef void (*sighdlr_t) (int, struct sigcontext *ctx);
typedef void (*sigaction_handler_t) (int, siginfo_t *, void *);

static sigset_t sig_received;   // signals currently blocked by all threads go here
static int sig_num_received;    // number of signals in sigs_received
static struct sigaction sigs[_NSIG];
static int sig_default_action[_NSIG];
static siginfo_t siginfo[_NSIG];		// saved siginfo for pending signals
static void *siguctx[_NSIG];			// save uctx argument for pending signals
static stack_t ss;
static sigset_t sigset_full, sigset_empty;

extern pointer_list_t *threadlist;

//extern int __libc_sigaction(int                    sig,
//                            const struct sigaction *act,
//                            struct       sigaction *oact);
static void signal_handler (int sig, struct sigcontext *ctx);
static void signal_sigaction (int sig, siginfo_t * info, void *uctx_v);

extern int __libc_sigaction(int                    sig,
                            const struct sigaction *act,
                            struct       sigaction *oact);

/**
 * make sure we exit nicely on signals
 **/
extern int exit_whole_program;
void
exit_signal_handler (int sig)
{
    (void) sig;
    // prepare to exit
    exit_whole_program = 1;
    syscall (SYS_exit, 0);
}

extern unsigned long long start_usec;

void
info_handler (int sig)
{
    output (" %lld - got signal %d (%s)\n", current_usecs () - start_usec,
            sig, sys_siglist[sig]);
}

void
abort_handler (int sig)
{
  if( sig != SIGINT )
    cap_override_rw = 0;

    {
      struct sigaction sa;
      sa.sa_flags = SA_ONSTACK;
      sa.sa_handler = SIG_DFL;
      
      if (__libc_sigaction (sig, &sa, NULL) != 0)
        warning ("error setting SIGABRT handler to SIG_DFL!!:  %s\n",
                 strerror (errno));
    }

    // re-raise the signal, to achive the default action
    raise(sig);
}

// signal handler, to request info dump 
void
debug_sighandler (int sig)
{
    (void) sig;
    dump_debug_info ();

    output ("\n--  Timers --\n");
    print_timers ();
    reset_timers ();

    if ( conf_show_thread_details )
        dump_thread_state ();

}

void
graphdump_sighandler (int sig)
{
    (void) sig;
    dump_blocking_graph ();
}


static void init_signals (void) __attribute__((constructor));
static void init_signals (void)
{
  static int init_done = 0;
  struct sigaction sa;
  int i;
  int rv;

  if (init_done)
    return;
  init_done = 1;

  // set up alt stack for all signals
  ss.ss_sp =
    mmap (0, SIGSTKSZ * 16, PROT_READ | PROT_WRITE,
          MAP_PRIVATE | MAP_ANON, -1, 0);
  assert (ss.ss_sp);
  ss.ss_size = SIGSTKSZ * 16;
  ss.ss_flags = 0;
  rv = sigaltstack (&ss, NULL);
  assert (rv == 0);

  // set up the sigaction data structure for each signal
  for (i = 0; i < _NSIG; i++)
    sig_default_action[i] = IGNORE;
  sig_default_action[SIGHUP] = TERMINATE;
  sig_default_action[SIGINT] = TERMINATE;
  sig_default_action[SIGQUIT] = TERMINATE;
  sig_default_action[SIGILL] = TERMINATE;
  sig_default_action[SIGTRAP] = TERMINATE;
  sig_default_action[SIGABRT] = TERMINATE;
  sig_default_action[SIGIOT] = TERMINATE;
  sig_default_action[SIGBUS] = TERMINATE;
  sig_default_action[SIGFPE] = TERMINATE;
  sig_default_action[SIGUSR1] = TERMINATE;
  sig_default_action[SIGSEGV] = TERMINATE;
  sig_default_action[SIGUSR2] = TERMINATE;
  sig_default_action[SIGPIPE] = TERMINATE;
  sig_default_action[SIGALRM] = TERMINATE;
  sig_default_action[SIGTERM] = TERMINATE;
  sig_default_action[SIGSTKFLT] = TERMINATE;
  sig_default_action[SIGCHLD] = IGNORE;
  sig_default_action[SIGURG] = IGNORE;
  sig_default_action[SIGXCPU] = TERMINATE;
  sig_default_action[SIGXFSZ] = TERMINATE;
  sig_default_action[SIGVTALRM] = TERMINATE;
  sig_default_action[SIGPROF] = TERMINATE;
  sig_default_action[SIGWINCH] = IGNORE;
  sig_default_action[SIGPOLL] = TERMINATE;
  sig_default_action[SIGIO] = TERMINATE;
  sig_default_action[SIGPWR] = TERMINATE;
  sig_default_action[SIGSYS] = TERMINATE;
  sig_default_action[SIGUNUSED] = TERMINATE;

  // set up wrapper handlers for all signals
  bzero (sigs, sizeof (struct sigaction) * _NSIG);
  for (i = 1; i < _NSIG; i++) {
    {
      //int ret = __libc_sigaction (i, NULL, &sa);
      int ret = syscall(SYS_sigaction, i, NULL, &sa);
      if( ret != 0 || sa.sa_handler != 0 ) 
        output("ret: %d  errno=%d  handler=%p \n",ret, errno, sa.sa_handler);
    }

    // save previous sigaction
    sigs[i].sa_mask = sa.sa_mask;
    sigs[i].sa_flags = sa.sa_flags;
    sigs[i].sa_handler = sa.sa_handler;
    sigs[i].sa_sigaction = sa.sa_sigaction;

    // setup wrapper
    sa.sa_flags |= SA_RESTART | SA_NOCLDSTOP | SA_ONSTACK;
    sa.sa_handler = (sighandler_t) signal_handler;

    // FIXME: blocking all signals from w/in every other handler
    // is ugly, since it thwarts ABORT, EXIT, INT, etc.  Better to
    // have signal handling functions actually be signal safe.
    // ;-) (One possibility is to just note the signal, and let
    // the scheduler twiddle the thread mask bits.
    sigfillset(&sa.sa_mask);
        
    // FIXME: allow ABORT and friends.  This currently ignores the default setting for this signal
    sigdelset(&sa.sa_mask, SIGABRT);
    sigdelset(&sa.sa_mask, SIGINT);
    sigdelset(&sa.sa_mask, SIGTERM);
    sigdelset(&sa.sa_mask, SIGBUS);
        

    if (i == SIGKILL || i == SIGSTOP)	// cannot handle these anyway
      continue;
        
    if (__libc_sigaction (i, &sa, NULL) != 0)
      warning ("error setting wrapper handler for signal %d: %s\n", i,
               strerror (errno));
  }

  // set some specific signal handlers for things we care about
  sa.sa_flags = SA_ONSTACK | SA_RESTART;
  sa.sa_handler = debug_sighandler;
  if (__libc_sigaction (SIGUSR1, &sa, NULL) != 0)
    fatal ("error signal handler for SIGUSR1: %s\n",
           strerror (errno));

  sa.sa_handler = graphdump_sighandler;
  if (__libc_sigaction (SIGUSR2, &sa, NULL) != 0)
    fatal ("error setting SIGUSR2 handler: %s\n", strerror (errno));

  sa.sa_handler = abort_handler;
  if (__libc_sigaction (SIGABRT, &sa, NULL) != 0)
    fatal ("error setting SIGABRT handler: %s\n", strerror (errno));
  if (__libc_sigaction (SIGSEGV, &sa, NULL) != 0)
    fatal ("error setting SIGSEGV handler: %s\n", strerror (errno));
  if (__libc_sigaction (SIGBUS, &sa, NULL) != 0)
    fatal ("error setting SIGBUS handler: %s\n", strerror (errno));
  if (__libc_sigaction (SIGFPE, &sa, NULL) != 0)
    fatal ("error setting SIGFPE handler: %s\n", strerror (errno));
  if (__libc_sigaction (SIGINT, &sa, NULL) != 0)
    fatal ("error setting SIGINT handler: %s\n", strerror (errno));

  sa.sa_handler = SIG_IGN;
  if (__libc_sigaction (SIGPIPE, &sa, NULL) != 0)
    fatal ("error setting SIGPIPE handler: %s\n", strerror (errno));

  sigemptyset(&sigset_empty);
  sigfillset(&sigset_full);

}

// context to save for signal handling in a thread
struct signal_context {
  int __errno;
  sigset_t mask;
};

// code run before each signal handler is called
// return 1 if we should proceed to call the signal handler, or 0 if we should just return
static int
signal_before (int sig, struct signal_context *ctx, siginfo_t *info, void *uctx_v) {
	
	// test if it is ignored
	if (! (sigs[sig].sa_flags & SA_SIGINFO) ) {
		if (sigs[sig].sa_handler == SIG_IGN
			|| (sigs[sig].sa_handler == SIG_DFL && sig_default_action[sig] == IGNORE)) {
				tdebug ("signal %d ignored\n", sig);
				return 0;
			}
	}
	
	// delay the signal if the signal is masked by the current thread
	// or we are in the scheduler
	if (!current_thread || sigismember(&current_thread->sig_mask, sig) 
		|| in_scheduler) {
		// try to deliver this signal to a thread that accepts it
	    linked_list_entry_t *e;
	    int delivered = 0;

		// FIXME: this is not safe!  The signal can happen when we are manipulating these things
		// somewhere
	    e = ll_view_head (threadlist);
	    while (e) {
    	    thread_t *t = (thread_t *) pl_get_pointer (e);
            //if (t == scheduler_thread || !thread_alive(t))
            if (!thread_alive(t))
	    		goto next_thread;
			if (!sigismember(&t->sig_mask, sig)) {
				if (!sigismember(&t->sig_received, sig)) {
					sigaddset (&t->sig_received, sig);
					t->sig_num_received++;
				}
				thread_resume (t);
				delivered = 1;
				tdebug ("signal %d will be delivered to thread %d\n", sig, t->tid);
				break;
			}
next_thread:
	        e = ll_view_next (threadlist, e);
    	}
		
		if (!delivered) {
			// all threads are blocking it, put it in the process signal pending list
			if (!sigismember(&sig_received, sig)) {
				sigaddset (&sig_received, sig);
				sig_num_received++;
			}
			tdebug ("signal %d now in global pending list\n", sig);
		}

		// save siginfo if the handler is SA_SIGINFO type
		if (sigs[sig].sa_flags & SA_SIGINFO) {
			memcpy (&siginfo[sig], info, sizeof(siginfo_t));
		}
		siguctx[sig] = uctx_v;
		return 0;
	}
	
	// save and set thread signal mask for signal handling
	ctx->__errno = errno;
	memcpy (&ctx->mask, &current_thread->sig_mask, sizeof (sigset_t));
	memcpy (&current_thread->sig_mask, &sigs[sig].sa_mask, sizeof (sigset_t));
	
	// mark the signal handled
	if (sigismember (&current_thread->sig_received, sig)) {
		sigdelset (&current_thread->sig_received, sig);
		current_thread->sig_num_received--;
	}
	if (sigismember (&sig_received, sig)) {
		sigdelset (&sig_received, sig);
		sig_num_received--;
	}

	// open global signal receiving
	syscall (SYS_sigprocmask, SIG_SETMASK, &sigset_empty, NULL);
	return 1;
}

static void
signal_after (struct signal_context *ctx) {
	errno = ctx->__errno;
	memcpy (&current_thread->sig_mask, &ctx->mask, sizeof (sigset_t));
	
	// process any signals happened during the process we handle this one
	sig_process_pending();
}

// wrapping handler for all signals (sa_handler signature)
static void
signal_handler (int sig, struct sigcontext *uctx)
{
	sighdlr_t func;
	struct signal_context ctx;
	
	tdebug ("sig=%d\n", sig);
	if (signal_before(sig, &ctx, NULL, uctx)) {
		func = (sighdlr_t)sigs[sig].sa_handler;
		if (func == (sighdlr_t)SIG_DFL) {
			assert (sig_default_action[sig] != IGNORE);	// already handled in signal_before()
			// then it must be TERMINATE
			tdebug ("Terminating due to signal %d\n", sig);
			exit (0);
		}
		tdebug ("Calling signal handler at %p\n", func);
		func (sig, uctx);
		signal_after(&ctx);
	}
}

// wrapping handler for all signals (sa_sigaction signature)
static void
signal_sigaction (int sig, siginfo_t * info, void *uctx_v)
{
	sigaction_handler_t func;
	struct signal_context ctx;
	
	tdebug ("sig=%d, uctx_v=%p\n", sig, uctx_v);
	if (signal_before (sig, &ctx, info, uctx_v)) {
		func = sigs[sig].sa_sigaction;
		tdebug ("Calling signal sigaction_handler at %p\n", func);
		func (sig, info, uctx_v);
		signal_after(&ctx);
	}
}

//////////////////////////////////////////////////////////////////////
// override standard syscalls
//////////////////////////////////////////////////////////////////////
#if 1
sighandler_t 
signal (int signum, sighandler_t handler)
{
	sighandler_t rt;
	struct sigaction sa;
	
	tdebug ("signum=%d, handler=%p\n", signum, handler);
	if (signum >= _NSIG || signum < 0 || signum == SIGKILL || signum == SIGSTOP) {
		rt = SIG_ERR;
		goto done;
	}
	rt = sigs[signum].sa_handler;
	sigs[signum].sa_handler = handler;
	sigs[signum].sa_flags = 0;
	
	// install corresponding global handler
	sa.sa_handler = (sighandler_t) signal_handler;
	sa.sa_flags = SA_RESTART | SA_NOCLDSTOP; //| SA_ONSTACK;
	sigfillset (&sa.sa_mask);
	sa.sa_restorer = 0;
	if (__libc_sigaction (signum, &sa, NULL) == -1)
        warning ("error setting global handler to signal %d!!:  %s\n", signum, 
                 strerror (errno));
	
done:
	tdebug ("=%p\n", rt);
	return rt;
}
#else
sighandler_t 
signal (int signum, sighandler_t handler)
{
	struct sigaction sa;
	sa.sa_handler = handler;
	sa.sa_flags = SA_RESTART | SA_NOCLDSTOP; //| SA_ONSTACK;
	sa.sa_restorer = 0;
	__libc_sigaction (signum, &sa, NULL);
	return 0;
}
#endif

strong_alias (signal, __signal);

int
sigaction (int sig, const struct sigaction *act, struct sigaction *oldact)
{
	int rt = 0;
	struct sigaction sa;

        // short circuit, for NULL arg
        if( act == NULL ) {
          return __libc_sigaction(sig, act, oldact);
        }

	tdebug ("sig=%d, act=%p, oldact=%p\n", sig, act, oldact);
	
	if (sig >= _NSIG || sig < 0 || sig == SIGKILL || sig == SIGSTOP) {
		rt = -1;
		errno = EINVAL;
		goto done;
	}
	if (oldact != NULL)
		memcpy (&oldact, &sigs[sig], sizeof (struct sigaction));
	sigs[sig].sa_sigaction = act->sa_sigaction;
	sigs[sig].sa_flags = act->sa_flags | SA_ONSTACK;
	memcpy (&sigs[sig].sa_mask, &act->sa_mask, sizeof (sigset_t));
	sigs[sig].sa_restorer = act->sa_restorer;
	
	if (act->sa_flags & SA_SIGINFO) {
		sa.sa_sigaction = signal_sigaction;
		sa.sa_flags = SA_SIGINFO | SA_RESTART | SA_NOCLDSTOP | SA_ONSTACK;
	} else {
		sa.sa_handler = (sighandler_t) signal_handler;
		sa.sa_flags = SA_RESTART | SA_NOCLDSTOP | SA_ONSTACK;
	}
	sa.sa_restorer = NULL;
	sigfillset (&sa.sa_mask);
	__libc_sigaction (sig, &sa, NULL);
	
done:
	tdebug ("=%d, errno=%d\n", rt, errno);
	return rt;
}

//strong_alias (sigaction, __sigaction);
// no alias for this because libc's sigaction() syscall calls __sigaction() internally

// Process all pending signals, call signal handler if any is recently unblocked
inline int
sig_process_pending() {
	int i;
	int rt = 0;
	if ( likely(current_thread->sig_num_received == 0 && sig_num_received == 0))
		goto done;
	for (i = 0; i < _NSIG; i++) {
		if (sigismember(&current_thread->sig_received, i)
			|| sigismember (&sig_received, i))
			if (!sigismember(&current_thread->sig_mask, i)) {
				// we handle the signal by re-delivering it because our handler is idempotent
				if (sigs[i].sa_flags & SA_SIGINFO)
					signal_sigaction (i, &siginfo[i], siguctx[i]);
				else
					signal_handler (i, siguctx[i]);
				// the 'received' flag is cleared by the signal handler
				rt = 1;
			}
	}
done:
	tdebug ("=%d\n", rt);	
	return rt;
}

int
sigprocmask (int how, const sigset_t * set, sigset_t * oldset)
{
	int rt = 0, i;
	tdebug ("how=%d, set=%p, oldset=%p\n", how, set, oldset);
	if (how != SIG_BLOCK && how != SIG_UNBLOCK && how != SIG_SETMASK) {
		rt = -1;
		errno = EINVAL;
		goto done;
	}
	if (oldset)
		memcpy (oldset, &current_thread->sig_mask, sizeof(sigset_t));
	switch (how) {
	case SIG_BLOCK:
		for (i = 0; i < _NSIG; i++)
			if (sigismember(set, i))
				sigaddset (&current_thread->sig_mask, i);
		break;
	case SIG_UNBLOCK:
		for (i = 0; i < _NSIG; i++)
			if (sigismember(set, i))
				sigdelset (&current_thread->sig_mask, i);
		break;
	case SIG_SETMASK:
		memcpy (&current_thread->sig_mask, set, sizeof (sigset_t));
		break;
	}
	// process any unblock signal if necessary
	// FIXME: going over all sigs is too slow and not necessary. we can do the decision above
	if (how == SIG_UNBLOCK || how == SIG_SETMASK)
		sig_process_pending();
done:
	tdebug ("=%d, errno=%d\n", rt, errno);
	return rt;
}

// don't alias this one, since it hoses abort() from glibc
//strong_alias(sigprocmask,__sigprocmask);

int
sigpending (sigset_t * set)
{
	int rt = 0;
	tdebug ("set=%p\n", set);
	memcpy (set, &current_thread->sig_received, sizeof (sigset_t));
	tdebug ("=%d\n", rt);
	return rt;
}

strong_alias (sigpending, __sigpending);


int
sigsuspend (const sigset_t * mask)
{
	int rt = -1;
    sigset_t saved_mask;

    tdebug ("mask=%p\n", mask);
    memcpy (&saved_mask, &current_thread->sig_mask, sizeof (sigset_t));
    memcpy (&current_thread->sig_mask, mask, sizeof(sigset_t));
    thread_suspend_self(0);		// wait forever for signal
	memcpy (&current_thread->sig_mask, &saved_mask, sizeof (sigset_t));

	errno = EINTR;
    tdebug ("=%d\n", rt);
    return rt;
}

strong_alias (sigsuspend, __sigsuspend);


// NOTE: this is probably specific to GLIBC
// FIXME: BSD style signal handling functions are not implemented 
// (anyway they are strongly deprecated)
int
__sigpause (int sig_or_mask, int is_sig)
{
    (void) sig_or_mask;
    (void) is_sig;
    assert (0);
    errno = EINVAL;
    return -1;
}


int
sigblock (int mask)
{
    (void) mask;
    assert (0);
    errno = ENOSYS;
    return -1;
}

strong_alias (sigblock, __sigblock);


int
siggetmask (void)
{
    assert (0);
    errno = ENOSYS;
    return -1;
}

strong_alias (siggetmask, __siggetmask);



int
sigsetmask (int mask)
{
    (void) mask;
    assert (0);
    errno = ENOSYS;
    return -1;
}

strong_alias (sigsetmask, __sigsetmask);


#undef sigmask
int
sigmask (int signum)
{
    (void) signum;
    assert (0);
    errno = ENOSYS;
    return -1;
}

strong_alias (sigmask, __sigmask);


/* Select any of pending signals from SET or wait for any to arrive.  */
int
sigwait (__const sigset_t * __restrict __set, int *__restrict __sig)
{
    return thread_sigwait (__set, __sig);
}

strong_alias (sigwait, __sigwait);



#ifdef __USE_POSIX199309
/* Select any of pending signals from SET and place information in INFO.  */
int
sigwaitinfo (__const sigset_t * __restrict __set,
             siginfo_t * __restrict __info)
{
    (void) __set;
    (void) __info;
    assert (0);
    errno = ENOSYS;
    return -1;
}

strong_alias (sigwaitinfo, __sigwaitinfo);

/* Select any of pending signals from SET and place information in INFO.
   Wait the time specified by TIMEOUT if no signal is pending.  */
int
sigtimedwait (__const sigset_t * __restrict __set,
              siginfo_t * __restrict __info,
              __const struct timespec *__restrict __timeout)
{
    (void) __set;
    (void) __info;
    (void) __timeout;
    assert (0);
    errno = ENOSYS;
    return -1;
}

strong_alias (sigtimedwait, __sigtimedwait);


/* Send signal SIG to the process PID.  Associate data in VAL with the signal.  */
int
sigqueue (__pid_t __pid, int __sig, __const union sigval __val)
{
    (void) __pid;
    (void) __sig;
    (void) __val;
    assert (0);
    errno = ENOSYS;
    return -1;
}

strong_alias (sigqueue, __sigqueue);

#endif


#if 0
int
kill (pid_t pid, int sig)
{
    int ret = syscall (SYS_kill, pid, sig);

    if (pid == getpid ())
        thread_kill_all (sig);

    return ret;
}

strong_alias (kill, __kill);
#endif


//////////////////////////////////////////////////////////////////////
// The thread lib internal versions of the functions
//////////////////////////////////////////////////////////////////////

int
thread_kill_all (int sig)
{
    linked_list_entry_t *e;
    int rt = 0;
    
	tdebug ("sig=%d\n", sig);

    e = ll_view_head (threadlist);
    while (e) {
        thread_t *t = (thread_t *) pl_get_pointer (e);
        if (thread_kill (t, sig) == -1) {
        	rt = -1;
            goto done;
        }
        e = ll_view_next (threadlist, e);
    }
done:
	tdebug ("=%d\n", rt);
    return rt;
}


int
thread_kill (thread_t * t, int sig)
{
	int rt = 0;
	tdebug ("t=%p, sig=%d\n", t, sig);
    if (sig < 0 || sig >= _NSIG) {
    	rt = -1;
        errno = EINVAL;
        goto done;
    }
    if (!valid_thread(t) || !thread_alive(t)) {
    	rt = -1;
    	errno = ESRCH;
    	goto done;
    }
    // this is cleared by the thread itself, after it has a chance to see which signals are available
	if (!sigismember (&t->sig_received, sig)) {
	    sigaddset (&t->sig_received, sig);
	    t->sig_num_received++;
	}

    if (!sigismember (&t->sig_mask, sig))
        thread_resume (t);

done:
	tdebug ("=%d, errno=%d\n", rt, errno);
    return rt;
}

int
thread_sigwait (const sigset_t * set, int *sig)
{
	sigset_t saved_mask;
    thread_t *t;
    int i, rt = 0;

	tdebug ("set=%p, sig=%p\n", set, sig);
    if (!set || !sig) {
        rt = -1;
        errno = EINVAL;
        goto done;
    }

    t = current_thread;

	// check if we already have that signal
collect:
	for (i = 1; i < _NSIG; i++) {
		if (!sigismember (set, i)) continue;
		if (sigismember(&t->sig_received, i) || sigismember (&sig_received, i) ) {
			*sig = i;
			if (sigismember (&t->sig_received, i)) {
				sigdelset (&t->sig_received, i);
				t->sig_num_received--;
			}
			if (sigismember (&sig_received, i)) {
				sigdelset (&sig_received, i);
				sig_num_received--;
			}
			goto done;
		}
	}

	// then we have to wait...
    // save old mask
    saved_mask = t->sig_mask;

    // set the new mask, it's just a bit map...
    for (i = 0; (unsigned)i < sizeof(sigset_t) / sizeof(unsigned long); i++)
    	((unsigned long *)&t->sig_mask)[i] = ~((unsigned long *)set)[i];

    // sleep
    CAP_SET_SYSCALL ();
	t->sig_waiting = 1;
    i = thread_suspend_self (0);
    assert (i == INTERRUPTED);		// we have to be waken up because of signals
    current_thread->sig_waiting = 0;
    CAP_CLEAR_SYSCALL ();

	t->sig_mask = saved_mask;
	
    // collect the delivered signal
	goto collect;

done:
	tdebug ("rt=%d, errno=%d\n", rt, errno);
	return rt;
}



/**
 * some ugly stuff, to allow dynamic linking to work.  There is undoubtedly a better way....
 **/
#include <setjmp.h>
extern void __libc_longjmp(jmp_buf env, int val) __attribute__((noreturn));
void longjmp(jmp_buf env, int val) {
  __libc_longjmp(env,val);
}
strong_alias (longjmp,__longjmp);

extern void __libc_siglongjmp(jmp_buf env, int val) __attribute__((noreturn));
void siglongjmp(jmp_buf env, int val) {
  __libc_siglongjmp(env,val);
}
strong_alias (siglongjmp,__siglongjmp);


int raise(int sig)
{
  return kill(getpid(), sig);
}

extern int __libc_fsync(int fd);
int fsync(int fd) { return __libc_fsync(fd); }



/**
 * Wrapper for system()
 *
 * FIXME: this currently just blocks the whole process.  This should
 * be fixed to just block the current thread.  The main poll loop then
 * needs to call wait(), to see if children have exited, and restart 
 * waiting threads appropriately.
 **/
int system(const char *string) { 
  int status;
  pid_t child, ret;
  sigset_t sigs;
  struct sigaction sa, sa_int, sa_quit;

  // ignore SIGINT and SIGQUIT
  bzero(&sa, sizeof(struct sigaction));
  sa.sa_handler = SIG_IGN;
  if (__libc_sigaction (SIGINT, &sa, &sa_int) != 0)
    fatal ("error setting SIGPIPE handler: %s\n", strerror (errno));
  if (__libc_sigaction (SIGQUIT, &sa, &sa_quit) != 0)
    fatal ("error setting SIGPIPE handler: %s\n", strerror (errno));
  
  // block SIGCHLD
  sigemptyset(&sigs);
  sigaddset(&sigs, SIGCHLD);
  syscall(SYS_sigprocmask, SIG_BLOCK, &sigs, NULL);

  
  // run the command
  child = fork();
  if( child < 0 )
    return -1;
  if( child == 0 ) {
    execl("/bin/sh", "/bin/sh", "-c", string, NULL);
    // NOTE: just exiting seems to behave badly - in particular, it kills the AIO layer.
    exit(0);  // FIXME: will this behave badly?  (ie, set main_exited, or some such?)
  }
  
  // wait for the child
  // FIXME: this will block the caller!
  while( (ret=waitpid(child, &status, 0)) == -1  &&  errno == EINTR ) ;
  if( ret == -1 ) {
    perror("waitpid");
  }

  // restore signal handlers
  syscall(SYS_sigprocmask, SIG_UNBLOCK, &sigs, NULL);
  if (__libc_sigaction (SIGINT, &sa_int, NULL) != 0)
    fatal ("error setting SIGPIPE handler: %s\n", strerror (errno));
  if (__libc_sigaction (SIGQUIT, &sa_quit, NULL) != 0)
    fatal ("error setting SIGPIPE handler: %s\n", strerror (errno));

  return status;
}



