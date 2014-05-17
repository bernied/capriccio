
#include <pthread.h>

#include <assert.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sched.h>
#include <unistd.h>


#include "cache.h"
#include "http.h"
#include "httphdrs.h"
#include "util.h"

#define pthread_mutex_lock(a) 
#define pthread_mutex_unlock(a) 

// FIXME: kludge - to print stats from capriccio w/o causing the scheduler to hang
#ifdef _CAPRICCIO_PTHREAD_H_
extern double open_rate;
extern double close_rate;
extern double avg_socket_lifetime;
void print_cap_stats() {
    printf("capstats:  opens %5.0f    closes %5.0f   lifetime %.0f ms\n", 
           open_rate, close_rate, avg_socket_lifetime);
}
#else
#define print_cap_stats() printf("\n");
#endif

//#include "blocking_graph.h"

#ifndef DEBUG_knot_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif

#define ALLOW_CHARS "/._"

#define USE_SENDFILE 0

#ifdef USE_CCURED
#define __START __attribute__((start))
#define __EXPAND __attribute__((expand))
#else
#define __START
#define __EXPAND
#endif

int g_use_timer = 1;
int g_spawn_on_demand = 0;
static int g_use_cache = 0;
static int g_force_thrashing = 0;

pthread_mutex_t g_cache_mutex;

static int g_conn_open=0;
static int g_conn_fail=0;
static int g_conn_succeed=0;
static int g_conn_active=0;
int g_cache_hits=0;
int g_cache_misses=0;

long long g_bytes_sent = 0;
static unsigned int g_timer_interval = 5; // in seconds

typedef struct thread_args thread_args;
struct thread_args
{
    int id;
    int s;
};

int
allow_file(char *file)
{
    // behold my feeble attempt at security

    char *p = file;
    int allow = 1;

    if (file[0] != '/')
    {
        allow = 0;
    }
    
    while (allow && *p != 0)
    {
        if (!(('a' <= *p && *p <= 'z') ||
              ('A' <= *p && *p <= 'Z') ||
              ('0' <= *p && *p <= '9') ||
              (strchr(ALLOW_CHARS, *p) != NULL)))
        {
            allow = 0;
        }

        p++;
    }

    if (allow)
    {
        if (strstr(file, "/.") != NULL)
        {
            allow = 0;
        }
    }

    return allow;
}

char *
get_request_filename(http_request *request)
{
    char *result = NULL;
    
    if (http_parse(request) && !request->closed)
    {
        assert(request->url[0] == '.');

        if (allow_file(request->url + 1))
        {
            result = request->url;
        }
    }

    return result;
}

int
get_request_fd(http_request *request)
{
    char *filename = get_request_filename(request);
    int fd = -1;

    if (filename != NULL)
    {
        fd = open(filename, O_RDONLY);
    }

    return fd;
}

int
process_client_nocache(http_request *request, int client)
{
    int fd = get_request_fd(request);
    int success = 1;

    if (fd >= 0)
    {
#if USE_SENDFILE > 0

NOTE-- this code is probably broken
        char buf[HEADER_200_BUF_SIZE];
        int buflen = 0;

        struct stat fd_stat;
        ssize_t size = 0;
        off_t offset = 0;

        if (fstat(fd, &fd_stat) < 0)
        {
            perror("fstat");
            exit(1);
        }

        size = fd_stat.st_size;

        buflen = snprintf(buf, HEADER_200_BUF_SIZE, HEADER_200,
                          "text/html", size);

        if (buflen < 0 || buflen >= HEADER_200_BUF_SIZE)
        {
            fprintf(stderr, "header buffer exceeded\n");
            exit(1);
        }

        if (write(client, buf, buflen) != buflen)
        {
            perror("write");
            exit(1);
        } else {
            pthread_mutex_lock(&g_cache_mutex);
            g_bytes_sent += buflen;
            pthread_mutex_unlock(&g_cache_mutex);
        }

        if (sendfile(client, fd, &offset, size) != size)
        {
            perror("sendfile");
            exit(1);
        }
#else
        char buf[8192];
        char *pos;
        int n = 1;
        int ret;        
        int written = 0;

        while (n > 0)
        {
            if ((n = read(fd, buf, sizeof(buf))) < 0)
            {
                perror("read");
                success = 0;
                break;
            }

            pos = buf;
            while( n > 0 ) {
                ret=write(client, pos, n);
                if ( ret < 0 )
                {
                    perror("write");
                    success = 0;
                    break;
                } else if (ret == 0) {
                    // GOT EOF
                    n = 0;
                    break;
                } else {
                    written += ret;
                    pos += ret;
                    n -= ret;
                }
            }
        }

        if (g_use_timer)
        {
            pthread_mutex_lock(&g_cache_mutex);
            g_bytes_sent += written;
            pthread_mutex_unlock(&g_cache_mutex);
        }
#endif


        close(fd);
    }

    return success;
}

cache_entry *
get_request_entry(http_request *request)
{
    char *filename = get_request_filename(request);

    if (filename == NULL) 
        return NULL;

    return cache_get(filename);
}

int
process_client_cache(http_request *request, int client)
{
    int success = 0;
    cache_entry *entry;

    //make_node();

    entry = get_request_entry(request);

    if (entry != NULL)
    {
        ssize_t written = 0;
        int n = 0;
        char *p, *bigstuff=NULL;
        
        // touch data on the stack, to force out other pages
#define BIGSIZE 1024*1024*700
        if( g_force_thrashing ) {
            bigstuff = malloc(BIGSIZE);  if( !bigstuff ) return 0;
            p = bigstuff + BIGSIZE - 3;
            while( p > bigstuff ) {
                *p = 10;
                p -= (4*1024); // presumably, pages are no smaller than 4k
            }
            sched_yield();
            free(bigstuff);
            bigstuff = NULL;
        }


        do
        {
            //make_node();
            n = write(client, entry->data + written, entry->total - written);

            if (n > 0)
            {
                written += n;
            
                pthread_mutex_lock(&g_cache_mutex);
                g_bytes_sent += n;
                pthread_mutex_unlock(&g_cache_mutex);
            }

        }
        while (n > 0 && written < entry->total);
        //make_node();

        // TODO: this needs work
        if (n < 0  &&  errno != EPIPE  &&  errno != ECONNRESET)
        {
            perror("warning: write");
        }

        if( g_force_thrashing ) {
            if(bigstuff) free(bigstuff);
        }


        success = 1;
    }

    //make_node();
    cache_entry_release(entry);

    return success;
}

void
process_client(int client)
{
    http_request request;
    int done = 0;
    int numrequests = 0;

    // don't send out partial frames
    /*
    { 
        int val = 1;
        if (setsockopt(c, SOL_TCP, TCP_CORK, (void *) &val, sizeof(val)) < 0)
        {
            perror("setsockopt");
            close(c);
            return;
        }
    }
    */

    http_init(&request, client);
    
    while (!done)
    {
        int success = g_use_cache ? process_client_cache(&request, client) :
                                    process_client_nocache(&request, client);

        pthread_mutex_lock(&g_cache_mutex);
        if( success ) {
            g_conn_succeed++;
        } else {
            //output("bad connection.  url='%s'\n",request.url);
            g_conn_fail++;
        }
        pthread_mutex_unlock(&g_cache_mutex);

        numrequests++;
        
        if (!success && !request.closed)
        {
            int len = strlen(HEADER_404);
            if (write(client, HEADER_404, len) != len)
            {
                ;
            } else {
                pthread_mutex_lock(&g_cache_mutex);
                g_bytes_sent += len;
                pthread_mutex_unlock(&g_cache_mutex);
            }

        }

        if (!success || request.version == HTTP_VERSION_1_0)
        {
            done = 1;
        }
    }

    // kludge.  subtract one failure for HTTP/1.1 requests, since the
    // last one always counts as failure.
    if( numrequests > 1 ) {
        pthread_mutex_lock(&g_cache_mutex);
        g_conn_fail--;
        pthread_mutex_unlock(&g_cache_mutex);
    }

}

void *
thread_process_client(void *arg)
{
    int c = (int) arg;
    process_client(c);
    pthread_mutex_lock(&g_cache_mutex);
    g_conn_active--;
    pthread_mutex_unlock(&g_cache_mutex);
    close(c);
    return NULL;
}

void
accept_loop(int id, int s) __EXPAND;
void
accept_loop(int id, int s)
{
    (void) id;
    //make_node();
    while (1)
    {
        struct sockaddr_in caddr;
        int len = sizeof(caddr);
        int c = 0;

        //make_node();

        debug("thread %d waiting\n", id);
        if ((c = accept(s, (struct sockaddr *) &caddr, &len)) < 0)
        {
            perror("accept");
            //exit(1);
            continue;
        }
        debug("thread %d done w/ accept\n", id);
        //make_node();
        
        pthread_mutex_lock(&g_cache_mutex);
        g_conn_open++;
        g_conn_active++;
        pthread_mutex_unlock(&g_cache_mutex);

        // turn off Nagle, so pipelined requests don't wait unnecessarily.
        if( 1 ) {
            int optval = 1;
            //if (setsockopt (sock, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof (optval)) < 0)
            if (setsockopt (c, SOL_TCP, TCP_NODELAY, &optval, sizeof (optval)) < 0)
            {
                perror("setsockopt");
                continue;
            }
        }


        debug("thread %d accepted connection\n", id);
        //make_node();


        if( g_spawn_on_demand ) {
            static int attr_init_done = 0;
            static pthread_attr_t attr;
            static pthread_t thread;

            if( !attr_init_done ) {
		int rv;
                pthread_attr_init( &attr );
                rv = pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
		assert (rv == 0);
                attr_init_done = 1;
            }
            
            if (pthread_create(&thread, &attr, &thread_process_client, (void *) c) < 0) {
                //perror("pthread_create");
                pthread_mutex_lock(&g_cache_mutex);
                g_conn_fail++;
                g_conn_active--;
                pthread_mutex_unlock(&g_cache_mutex);
                close(c);
            }
        } else {
            process_client(c);
            close(c);
            pthread_mutex_lock(&g_cache_mutex);
            g_conn_active--;
            pthread_mutex_unlock(&g_cache_mutex);
            //make_node();
        }

    }
}









void *
thread_main_autospawn(void *arg)
{
    int s = (int) arg;

    accept_loop(-1, s);

    return NULL;
}


void *
thread_main(void *arg) __START __EXPAND;
void *
thread_main(void *arg)
{
    thread_args *targs = (thread_args *) arg;
    int id = targs->id;
    int s = targs->s;

    free(targs);

    //printf("thread %d starting\n", id);
    accept_loop(id, s);
    //printf("thread %d stopping\n", id);

    return NULL;
}

int
main(int argc, char **argv)
{
    struct sockaddr_in saddr;
    int s = 0;

    int nthreads = 0;
    int i = 0;

    int val = 1;

    if (argc != 3 && argc != 4)
    {
        fprintf(stderr, "usage: %s port threads [root]\n", argv[0]);
        exit(1);
    }

    if (argc == 4)
    {
        printf("setting root directory to [%s]\n", argv[3]);
        if (chdir(argv[3]) < 0)
        {
            perror("chdir");
            exit(1);
        }
    }

    pthread_mutex_init(&g_cache_mutex, NULL);
    cache_init();

    if( !strcmp(argv[2], "auto") ) 
    {
        g_spawn_on_demand = 1;
    } 
    else 
    {
        nthreads = atoi(argv[2]);
        if (nthreads <= 0) {
            fprintf(stderr, "nthreads should be > 0 or 'auto'\n");
            exit(1);
        }
    }
    
    if ((s = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket");
        exit(1);
    }

    val = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0)
    {
        perror("setsockopt");
        exit(1);
    }

    saddr.sin_family = PF_INET;
    saddr.sin_port = htons(atoi(argv[1]));
    saddr.sin_addr.s_addr = INADDR_ANY;
    if (bind(s, (struct sockaddr *) &saddr, sizeof(saddr)) < 0)
    {
        perror("bind");
        exit(1);
    }

    if (listen(s, 50000) < 0)
    {
        perror("listen");
        exit(1);
    }

    // spawn initial threads
    if (g_spawn_on_demand) 
    {
        pthread_t thread;

        if (pthread_create(&thread, NULL, &thread_main_autospawn, (void*) s) < 0)
        {
            perror("pthread_create");
        }
    }
    else 
    {
        printf("Spawning %d threads....\n",nthreads);
        for (i = 0; i < nthreads; i++)
        {
            thread_args *targs = NULL;
            pthread_t thread;
            
            targs = (thread_args *) malloc(sizeof(*targs));
            assert(targs != NULL);
            targs->id = i;
            targs->s = s;
            
            debug("spawning thread %d\n",i);
            if (pthread_create(&thread, NULL, &thread_main, (void *) targs) < 0)
            {
                perror("pthread_create");
            }
        }
        printf("done\n");
    }

    if (g_use_timer)
    {
        unsigned long long start, now;

        start = current_usecs();
        sleep(g_timer_interval);

        while (1)
        {
            long long bytes_sent;
            int conn_open, conn_succeed, conn_fail, conn_active, cache_hits, cache_misses;

            now = current_usecs();
            
            pthread_mutex_lock(&g_cache_mutex);
            bytes_sent   = g_bytes_sent;      g_bytes_sent = 0;
            conn_open    = g_conn_open;       g_conn_open = 0;
            conn_succeed = g_conn_succeed;    g_conn_succeed = 0; 
            conn_fail    = g_conn_fail;       g_conn_fail = 0;
            conn_active  = g_conn_active;
            cache_hits   = g_cache_hits;      g_cache_hits = 0;
            cache_misses = g_cache_misses;    g_cache_misses = 0;
            pthread_mutex_unlock(&g_cache_mutex);

            
            //printf("rate: %.3g Mbits/sec   %.0f open/sec   %.0f succ/sec   %.0f fail/sec\n",
            printf("rate: %.3g Mbits/sec   %.0f open/sec   %.0f succ/sec   %.0f fail/sec   active: %d   misses: %d   hitrate: %.1f%%   ",
                   ((double)bytes_sent * 8 * 1000000) / ((double)(now-start) * (1024 * 1024)),
                   ((double)conn_open * 1000000) / (double)(now-start),
                   ((double)conn_succeed * 1000000) / (double)(now-start),
                   ((double)conn_fail * 1000000) / (double)(now-start),
                   conn_active,
                   cache_misses,
                   100*((double)cache_hits)/(double)(cache_hits+cache_misses)
                   );
            print_cap_stats();


            start = now;
            
            sleep(g_timer_interval);
        }
    }

    pthread_exit(0);

    return 0;

    //close(s);
    //exit(0);
}


//////////////////////////////////////////////////
// Set the emacs indentation offset
// Local Variables: ***
// c-basic-offset:4 ***
// End: ***
//////////////////////////////////////////////////
