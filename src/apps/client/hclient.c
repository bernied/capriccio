
#include <pthread.h>

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <stdlib.h>
#include <sched.h>
#include <signal.h>

#include <netinet/tcp.h>
#include <netinet/in.h>
#include <pthread.h>

// definitions specific for capriccio client
#include "util.h"
#include "histogram.h"
#include "specload.h"

#define SEND_BUFFER_SIZE 1024
#define RECV_BUFFER_SIZE 1024*32

#ifndef DEBUG_hclient_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif


// timing histograms
histogram_t *connect_hist;
histogram_t *err_connect_hist;
histogram_t *request_hist;
histogram_t *response_hist;
histogram_t *close_hist;
histogram_t *close_unconnected_hist;
histogram_t *sleep_hist;

// FIXME: this looses anything over 100 ms.  Need better histogram.
#define HIST_BUCKETS 100*1000
#define HIST_INTERVAL(start,end) ( (end-start) / ticks_per_microsecond )


// general stats
static unsigned long long bytes_read=0, bytes_written=0;
static unsigned long long good_conn=0, bad_conn=0;
static unsigned long long io_errors = 0;
static unsigned long long conn_timedout = 0;
static unsigned long long addrnotavail = 0;
static unsigned long long bind_port_calls = 0;
static unsigned long long ports_checked = 0;
static unsigned long long requests_completed = 0;


static unsigned int state_idle = 0;
static unsigned int state_connecting = 0;
static unsigned int state_writing = 0;
static unsigned int state_reading = 0;
static unsigned int state_closing = 0;


// server machine defs
typedef struct {
  struct sockaddr_in addr;
  int port;
  char *hostname;
  char *url;
} server_t;

#define MAX_SERVERS 20
static int num_servers;
static server_t servers[MAX_SERVERS];
static int nreqs = 5;
static int sleep_ms = 1000;
static int spec_load = 1;
static int mdwstyle = 0;
static int total_reports = 20;

#define STATS_INTERVAL_SECS 5

// compute the difference b/w two timevals
#define TVDIFF(end,start) (\
  end.tv_usec > start.tv_usec ? \
  ((end.tv_sec-start.tv_sec)*1e6 + end.tv_usec - start.tv_usec) :\
  ((end.tv_sec-start.tv_sec-1)*1e6 + end.tv_usec + (1e6 - start.tv_usec)) \
)


static int debug = 0;

/**
 * Maintain a list of "free" ports.  This was inspired by httperf, but is generally simpler
 **/
#define MIN_IP_PORT	IPPORT_RESERVED
#define MAX_IP_PORT	65535
//#define BITSPERLONG	(8*sizeof (u_long))

// FIXME: it is more efficient to _not_ check the ports that are known
// to be used internally.  This may not matter if connections
// generally have similar lifetimes, though, as by the time we cycle
// through all possible ports, we should have mostly empty ones again.

int bind_port( int sock )
{
  static int init_done = 0;
  static int port = MIN_IP_PORT;
  static struct sockaddr_in addr;
  int startport;
  int res;
  int my_ports_checked=0;
  int status = 1;

  if( !init_done ) {
    init_done = 1;
    bzero(&addr, sizeof(addr));
  }

  bind_port_calls++;

  // find a free port to bind to.
  startport = port;
  while (1) {

    ports_checked++;
    my_ports_checked++;

    port++;
    if( port > MAX_IP_PORT ) port = MIN_IP_PORT;
    if( port == startport ) {
      fprintf(stderr,"client is out of ports!!\n");
      status = 0;
      break;
    }

    addr.sin_port = htons(port);
    res = bind(sock, (struct sockaddr*) &addr, sizeof(addr));
    if( res == 0 ) {
      status = 1;
      break;
    }


    //if (errno != EADDRINUSE && errno == EADDRNOTAVAIL)
    if( errno != EADDRINUSE ) {
      fprintf(stderr,"bind failed for port %d: %s\n", port, strerror(errno));
      io_errors++;
      status = 0;
      break;
    }
  
  }

  if( debug && my_ports_checked > 10 )
    printf("checked %d ports\n",my_ports_checked);

  return status;
}


/**
 * Write all of the data, or return an error
 **/
int write_all( int sock, char *buf, int len )
{
  int ret;

  while( len > 0 ) {
    ret = write( sock, buf, len );
    if( ret < 0 ) {
      if( errno != EINTR ) {
        if( debug ) printf("write_all() - error with write: %s\n",strerror(errno));
        io_errors++;
        return -1;
      }
    } else {
      buf += ret;
      len -= ret;
      bytes_written += ret;
    }
  }

  return 0;
}

/**
 * read and discard data
 **/
int read_all( int sock, int len )
{
  static char buf[16*1024];  // global var, since we're throwing out the data anyway
  int ret;

  while( len > 0 ) {
    //output("   reading data...\n");
    ret = read( sock, buf, len > (int) sizeof(buf) ? (int) sizeof(buf) : len );
    if( ret < 0 ) {
      if( errno != EINTR ) {
        if( debug ) printf("read_all() - error with read: %s\n",strerror(errno));
        io_errors++;
        return -1;
      }
    } else if( ret == 0 ) {
      if( debug ) printf("read_all() - file closed w/ %d bytes remaining\n", len);
      io_errors++;
      return -1;
    } else {
      len -= ret;
      bytes_read += ret;
      //printf("   got %d bytes - %d remaining...\n", ret, len);
    }
  }

  return 0;
}


/**
 * read an HTTP response.  We assume that we can get the entire response at once.
 **/
int read_response( int sock )
{
  char buf[1024]; // stack allocated, since we have concurrency
  char *p;
  int len, headlen=0, datalen=0, dataread=0;
  int space = sizeof(buf)-1;

  //output("   reading header\n");
  p = buf;
  *p = '\0';
  while( strstr(buf,"\r\n\r\n") == NULL ) {
    while( (len = read( sock, p, space )) < 0  &&  errno == EINTR ) ;
    if( len < 0 ) {
      if( debug ) printf("read_response() - error reading header: %s\n", strerror(errno));
      io_errors++;
      return -1;
    }
    p += len; *p = '\0';
    bytes_read += len;
    if( len == 0 ) {
      if( debug ) printf("read_response() got EOF while reading header\n");
      break;
    }

    // crap out if we are out of space
    space -= len;
    if( space <= 0 )
      break;
  }
  headlen = p - buf;
  
  //if( debug ) printf("head len=%d\n%s\n", headlen, buf);
  
  // parse out response

  p = strstr(buf, "200 OK");  
  if( !p ) {
    if( debug ) printf("Got HTTP Error response\nheadlen=%d\n%s\n\n",headlen,buf);
    return -1;
  }

  // read the content length, if any
  p = strstr(p, "Content-Length: ");  
  if( !p ) {
    bad_conn++;
    if( debug ) printf("Error - no Content-Length header!\n");
    return -1;
  } else {
    char *end;
    p += strlen("Content-Length: ");
    datalen = (int) strtol(p, &end, 0);
    if( end == p ) {
      bad_conn++;
      if( debug ) printf("Error - bad number in Content-Length header!\n");
      return -1;
    }
  }

  // find the end of the header
  p = strstr(p, "\r\n\r\n");          
  if( p ) p += 4;
  else {
    bad_conn++;
    if( debug ) printf("Error - end of HTTP header not found\n");
    return -1;
  }

  // subtract out any amount of data we've already read
  if( datalen && p )
    datalen -= (headlen - (p-buf));
  
  // read the response data
  if( datalen > 0 )
    dataread = read_all(sock,datalen);
  if( dataread != 0 ) {
    bad_conn++;
    if( debug ) printf("error from read_all()\n");
    return -1;
  }
  
  return 0;
}

void* client(void *arg)
{
  char request[1024];
  int sock, ret, i, len;
  cpu_tick_t start, end;
  int server_num = (int) arg;
  int did_some_io = 1;
  int optval;


  // loop, making requests, & keeping stats
  while( 1 ) {
    state_idle++;
  

    // yield, if we didn't yet, ie, due to socket() or connect() failures
    if( !did_some_io )
      sched_yield();
    did_some_io = 0;


    state_idle--; 
    state_connecting++;

    // new socket
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if( sock == -1 ) { 
      if( debug ) printf("client() - error with socket(): %s\n", strerror(errno));
      io_errors++;  //perror("socket"); 
      state_connecting--;
      continue; 
    }

    // turn off TCP linger, to do a quick close of the socket
    if ( 0 ) {
      struct linger linger;
      linger.l_onoff = 1;
      linger.l_linger = 0;
      if (setsockopt (sock, SOL_SOCKET, SO_LINGER, &linger, sizeof (linger)) < 0) {
        if( debug ) printf("client() - error setting SO_LINGER: %s\n", strerror(errno));
        io_errors++;
        state_connecting--;
        continue;
      }
    }


    // make the sockets reusable, so we don't wait as long for the
    // kernel to clean things up, and hence run out of sockets.
    //
    // FIXME: is this only meaningful for server sockets??
    optval = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
      if( debug ) printf("client() - error setting SO_REUSEADDR: %s\n", strerror(errno));
      io_errors++;
      state_connecting--;
      continue;
    }

    /*
    optval = SEND_BUFFER_SIZE;
    if (setsockopt (sock, SOL_SOCKET, SO_SNDBUF, &optval, sizeof (optval)) < 0)
    {
      if( debug ) printf("client() - error setting SO_SNDBUF: %s\n", strerror(errno));
      io_errors++;
      state_connecting--;
      continue;
    }
    
    optval = RECV_BUFFER_SIZE;
    if (setsockopt (sock, SOL_SOCKET, SO_RCVBUF, &optval, sizeof (optval)) < 0)
    {
      if( debug ) printf("client() - error setting SO_RCVBUF: %s\n", strerror(errno));
      io_errors++;
      state_connecting--;
      continue;
    }
    */

    // Disable the Nagle algorithm so the kernel won't delay our
    // writes waiting for more data.
    if( 1 ) {
      optval = 1;
      //if (setsockopt (sock, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof (optval)) < 0)
      if (setsockopt (sock, SOL_TCP, TCP_NODELAY, &optval, sizeof (optval)) < 0)
      {
        if( debug ) printf("client() - error setting TCP_NODELAY: %s\n", strerror(errno));
        io_errors++;
        state_connecting--;
        continue;
      }
    }

    if ( !bind_port(sock) ) {
      state_connecting--;
      continue;
    }

    // Disable the Nagle algorithm so the kernel won't delay our
    // writes waiting for more data.
    if( 1 ) {
      optval = 1;
      //if (setsockopt (sock, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof (optval)) < 0)
      if (setsockopt (sock, SOL_TCP, TCP_NODELAY, &optval, sizeof (optval)) < 0)
      {
        if( debug ) printf("client() - error setting TCP_NODELAY: %s\n", strerror(errno));
        io_errors++;
        state_connecting--;
        continue;
      }
    }

    // connect
    //output("connecting...\n");
    server_num++; server_num = server_num % num_servers;
    GET_REAL_CPU_TICKS(start);
    ret = connect(sock, (struct sockaddr *)&servers[server_num].addr, sizeof(servers[0].addr));
    GET_REAL_CPU_TICKS(end);
    if( ret < 0 ) { 
      state_connecting--; 
      hist_add(err_connect_hist, HIST_INTERVAL(start,end));

      if( errno == EADDRNOTAVAIL ) {
        addrnotavail++;
      } else {
        io_errors++; 
        if( debug )
          printf("client() - error with connect(): %s\n", strerror(errno));
      }

      GET_REAL_CPU_TICKS(start);
      state_closing++;
      close(sock);
      state_closing--;
      GET_REAL_CPU_TICKS(end);
      hist_add(close_unconnected_hist, HIST_INTERVAL(start,end));

      continue; 
    }
    hist_add(connect_hist, HIST_INTERVAL(start,end));


    // Disable the Nagle algorithm so the kernel won't delay our
    // writes waiting for more data.
    if( 1 ) {
      optval = 1;
      //if (setsockopt (sock, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof (optval)) < 0)
      if (setsockopt (sock, SOL_TCP, TCP_NODELAY, &optval, sizeof (optval)) < 0)
      {
        if( debug ) printf("client() - error setting TCP_NODELAY: %s\n", strerror(errno));
        io_errors++;
        state_connecting--;
        continue;
      }
    }

    state_connecting--;

    // issue requests
    for( i=0; i<nreqs; i++ ) {
      len = snprintf(request, sizeof(request)-1, 
                     "GET /%s/dir%05d/class%d_%d HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     //"User-Agent: Mozilla/4.0 (compatible; MSIE 4.01; windows 98)\r\n"
                     //"Accept: */*\r\n"
                     //"Accept-Encoding: gzip, deflate\r\n"
                     //"Accept-Language: en/us\r\n"
                     //"Connection: Close\r\n"
                     "\r\n", 
                     servers[server_num].url, spec_dir(), spec_class(), spec_file(),
                     servers[server_num].hostname
                     );
      assert(len > 0);

      // note that we did some IO, so this thread has already yielded.
      did_some_io = 1;

      state_writing++;
      GET_REAL_CPU_TICKS(start);
      ret = write( sock, request, len );
      GET_REAL_CPU_TICKS(end);
      hist_add(request_hist, HIST_INTERVAL(start,end));
      state_writing--;

      // we do this here, instead of after connect b/c w/ Capriccio
      // the connect() call seems to always return immediately.
      good_conn++;

      if( len != ret ) {
        // FIXME: for now, don't add anything to the response hist, if we get a failure here
        if( errno == ETIMEDOUT ) {
          conn_timedout++;
        } else {
          if(debug) 
            printf("client() - error writing request: %s\n", strerror(errno));
          io_errors++;
        }
        break;
      }

        //output("reading response...\n");
      state_reading++;
      GET_REAL_CPU_TICKS(start);
      if( read_response( sock ) == 0 )
        requests_completed++;
      else 
        bad_conn++;
      GET_REAL_CPU_TICKS(end);
      hist_add(response_hist, HIST_INTERVAL(start,end));
      state_reading--;
      // FIXME: close the connection on errors?


      // sleep for a second
      if(i < nreqs-1 && sleep_ms > 0) {
        state_idle++;
        GET_REAL_CPU_TICKS(start);
        usleep(sleep_ms*1000);
        GET_REAL_CPU_TICKS(end);
        hist_add(sleep_hist, HIST_INTERVAL(start,end));
        state_idle--;
      }

    }

    //output("closing...\n");
    state_closing++;
    GET_REAL_CPU_TICKS(start);
    close( sock );
    GET_REAL_CPU_TICKS(end);
    hist_add(close_hist, HIST_INTERVAL(start,end));
    state_closing--;
  }

  return NULL;
}


void usage(char *msg)
{
  if( msg ) fprintf(stderr,"%s\n\n", msg);
  fprintf(stderr, "format: client -u URL[,URL,...] -n NTHREADS -r NREQS -s SLEEP_MS -d\n");

  if( msg ) exit(-1);
  else exit(0);
}

int main(int argc, char **argv)
{
  int i, nthreads=-1;
  char *server_string = NULL;
  char opt;

  // deal w/ args
  while ( (opt = getopt(argc, argv, "ml:t:n:u:r:s:d")) != (char)EOF) {
    switch ( opt ) {
    case 'm':
      mdwstyle = 1;
      break;
    case 'l':
      spec_load = atoi(optarg);
      if( spec_load <= 0 ) usage("Invalid load level");
      break;
    case 't':
      total_reports = atoi(optarg);
      if( total_reports <= 0 ) usage("Invalid number of reports");
      break;
    case 'n':
      nthreads = atoi(optarg);
      if( nthreads <= 0 ) usage("Invalid number of threads");
      break;
    case 'u':
      server_string = optarg;
      break;
    case 'd':
      debug = 1;
      break;
    case 'r':
      nreqs = atoi(optarg);
      if( nreqs <= 0 ) usage("Invalid option for -n NUM_REQUESTS");
      break;
    case 's':
      sleep_ms = atoi(optarg);
      if( sleep_ms < 0 ) usage("Invalid option for -s SLEEP_MS");
      break;
    case '?':
      usage(NULL);
    default:
      usage("Invalid option");
    }
  }

  if( nthreads == -1 )
    usage("You must specify the number of threads.");
  if( server_string == NULL )
    usage("You didn't specify the base URL(s).");

  // set up the SPEC load generation
  setupDists(spec_load);

  // create some histograms
  connect_hist = hist_new(HIST_BUCKETS, 0); 
  err_connect_hist = hist_new(HIST_BUCKETS, 0); 
  request_hist = hist_new(HIST_BUCKETS, 0); 
  response_hist = hist_new(HIST_BUCKETS, 0); 
  close_hist = hist_new(HIST_BUCKETS, 0); 
  close_unconnected_hist = hist_new(HIST_BUCKETS, 0); 
  sleep_hist = hist_new(HIST_BUCKETS, 0); 

  // set up addres, ports, etc. for the servers
  {
    struct hostent *hostelement;
    char *str = server_string;
    char *end, c;
    int i=0;

    bzero( servers, sizeof(servers) );

    while( str != NULL  &&  *str != '\0' ) {
      // skip past http://
      if( strncasecmp(str,"http://",7) == 0 )
        str += 7;

      // read the hostname
      servers[i].hostname = str;
      str += strcspn(str,",/:");
      if( servers[i].hostname == str ) 
        usage("Invalid URL, hostname not found");
      c = *str; *str = '\0'; str++;

      // read the port
      if( c == ':' ) {
        servers[i].port = (int) strtol(str, &end, 0);
        if( servers[i].port == 0 || str == end ) 
          usage("Invalid port in URL.");
        str = end;
      } else {
        servers[i].port = 80;
      }

      // read the base URL
      str += strspn(str,"/");
      servers[i].url = str;
      
      // update the server structure
      servers[i].addr.sin_family = AF_INET;
      servers[i].addr.sin_port = htons((short)servers[i].port);

      hostelement = gethostbyname(servers[i].hostname);
      assert( hostelement );
      memcpy(&servers[i].addr.sin_addr, hostelement->h_addr, hostelement->h_length);


      printf("server %d:  host=%s  port=%d  url=%s\n",
             i, servers[i].hostname, servers[i].port, servers[i].url );

      // increment the number
      i++;
      if( i >= MAX_SERVERS )
        usage("Too many servers specified.");

      // end the URL string
      str += strcspn(str,",");
      if( *str == '\0' )
        break;
      *str = '\0';
      str++;

    }

    num_servers = i;

  }

  // spawn client threads
  {
    int rv;
    pthread_attr_t attr;
    pthread_t thread;

    pthread_attr_init( &attr );
    rv = pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
    assert (rv == 0);

    for( i=0; i<nthreads; i++ ) {
      int rv;
      rv = pthread_create(&thread, &attr, &client, (void*)i);
      assert( rv == 0);
    }
  }

  
  // periodically print stats
  { 
    cpu_tick_t prevstart;
    cpu_tick_t start, end;
    struct timeval tv_start, tv_prev;
    int num_reports_done = 0;

    // initial delay
    usleep( (STATS_INTERVAL_SECS*1e6) );

    GET_REAL_CPU_TICKS(prevstart);
    gettimeofday(&tv_prev, NULL);
    
    while( 1 ) {

      unsigned long long s_bytes_read = bytes_read;
      unsigned long long s_bytes_written = bytes_written;
      unsigned long long s_good_conn = good_conn;
      unsigned long long s_bad_conn = bad_conn;
      unsigned long long s_conn_timedout = conn_timedout;
      unsigned long long s_io_errors = io_errors;
      unsigned long long s_addrnotavail = addrnotavail;
      unsigned long long s_requests_completed = requests_completed;

      //unsigned long long s_bind_port_calls = bind_port_calls;
      //unsigned long long s_ports_checked = ports_checked;

      int connect_mean = hist_mean(connect_hist);
      int err_connect_mean = hist_mean(err_connect_hist);
      int request_mean = hist_mean(request_hist);
      int response_mean = hist_mean(response_hist);
      int close_mean = hist_mean(close_hist);
      int close_unconnected_mean = hist_mean(close_unconnected_hist);
      int sleep_mean = hist_mean(sleep_hist);
      hist_reset(connect_hist);
      hist_reset(err_connect_hist);
      hist_reset(request_hist);
      hist_reset(response_hist);
      hist_reset(close_hist);
      hist_reset(close_unconnected_hist);
      hist_reset(sleep_hist);

      

      GET_REAL_CPU_TICKS(start);
      gettimeofday(&tv_start, NULL);

      requests_completed = 0;
      addrnotavail = 0;
      bind_port_calls = 0;
      ports_checked = 0;
      io_errors = 0;
      bad_conn = 0;
      conn_timedout = 0;
      good_conn = 0;
      bytes_written = 0;
      bytes_read = 0;

      printf("(%.1f %.1f)  Mb/s: %5.2fr %5.2fw    conn/s: %5llu (%llu %4llu)   io err/s: %llu (%4llu)  -- idle: %d (%d)   conn: %d (%d,%d)   write: %d (%d)   read %d (%d)   close %d (%d,%d)\n",
             ((float)(start-prevstart)) / ticks_per_second,
             ((float)TVDIFF(tv_start,tv_prev)) / 1e6,
             ((float)s_bytes_read)*8/1024/1024 / STATS_INTERVAL_SECS, 
             ((float)s_bytes_written)*8/1024/1024 / STATS_INTERVAL_SECS, 
             s_good_conn/STATS_INTERVAL_SECS, 
             s_bad_conn/STATS_INTERVAL_SECS, 
             s_conn_timedout/STATS_INTERVAL_SECS,
             s_io_errors/STATS_INTERVAL_SECS, 
             s_addrnotavail/STATS_INTERVAL_SECS,

             state_idle, sleep_mean,
             state_connecting, connect_mean, err_connect_mean, 
             state_writing, request_mean, 
             state_reading, response_mean, 
             state_closing, close_mean, close_unconnected_mean
             ); 

      //0 BT: 4047705 5.174
      if( mdwstyle ) {
        printf("Connection Rate: %f connections/sec, %llu conns\n", ((float)s_good_conn)/STATS_INTERVAL_SECS, s_good_conn);
        printf("Overall rate:	%f completions/sec\n",    ((float)s_requests_completed)/STATS_INTERVAL_SECS);
        printf("Bandwidth:	%f bytes/sec\n", ((float)s_bytes_read) / STATS_INTERVAL_SECS); 

        // FIXME: add histograms, and fix these up
        //printf("Connect Time:	%f ms, max %d ms\n", 0, 0);
        //printf("Response Time:	%f ms, max %d ms\n", 0, 0);
        //printf("Combined Response Time:	%f ms, max %d ms\n", 0, 0);
      }

      fflush(stdout);

      num_reports_done++;
      if(num_reports_done >= total_reports) {
        // kludgey way to force an exit
        kill(getpid(), SIGINT);
        kill(getpid(), SIGQUIT);
        kill(getpid(), SIGKILL);
      }        


      GET_REAL_CPU_TICKS(end);

      usleep( (STATS_INTERVAL_SECS*1e6) - (end-start)/ticks_per_microsecond );

      prevstart = start;
      tv_prev = tv_start;
    }
  }


  return 0;
}


