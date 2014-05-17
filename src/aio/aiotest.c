#include "threadlib.h"
#include "blocking_io.h"
#include "util.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>


// comment out, to enable debugging in this file
#ifndef DEBUG_aiotest_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif


#define checkreturn(a,b) \
  if( a ) {\
    warning("%s() failed - %s\n",(b), strerror(errno)); \
    thread_exit_program(1); \
  }

void banner(char *msg)
{
  output("\n\n----------------------------------------------------------------------\n");
  output("STARTING TEST:  %s\n",msg);
  output("----------------------------------------------------------------------\n");
}

//////////////////////////////////////////////////////////////////////
// socket IO testing routines
//////////////////////////////////////////////////////////////////////

void* sockio_test_server(void *arg)
{
  struct sockaddr_in sar;
  struct sockaddr_in peer_addr;
  int opt = 1;
  socklen_t optlen = sizeof(opt);
  socklen_t peer_len;
  int sr;
  int port = 8081;
  int sock;
  FILE *client;
  (void) arg;
  
  output("    server - creating socket\n");
  checkreturn( ((sock = socket(PF_INET, SOCK_STREAM, 0)) == -1), "socket");
  setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&opt,optlen);
  output("    server - socket created\n");

  // bind
  sar.sin_family      = AF_INET;
  sar.sin_addr.s_addr = INADDR_ANY;
  sar.sin_port        = htons(port);
  output("    server - calling bind\n");
  checkreturn( (bind(sock, (struct sockaddr *)&sar, sizeof(struct sockaddr_in)) == -1), "bind");
  output("    server - bind succeeded\n");
  
  // start listening on the socket with a queue of 10
  output("    server - calling listen\n");
  checkreturn( (listen(sock, 100) == -1), "listen");
  output("    server - listening on port %d\n", port);

  // accept next connection
  peer_len = sizeof(peer_addr);
  output("    server - waiting for connection\n");
  checkreturn( ((sr = accept(sock, (struct sockaddr *)&peer_addr, &peer_len)) == -1), "accept");
  output("    server - connection established (fd: %d, ip: %s, port: %d)\n",
         sr, inet_ntoa(peer_addr.sin_addr), ntohs(peer_addr.sin_port));

  close(sock);

  // mini session
  checkreturn( ((client = fdopen(sr,"a+")) == NULL), "fdopen");

  output("    server - writing data\n");
  fprintf(client, "hello to you, client!");
  fclose(client);
  close(sr);
  output("    server - data written\n");

  return NULL;
}


void* sockio_test_client(void *arg)
{
  struct sockaddr_in addr;
  char buf[1024];
  int port = 8081;
  int sock, ret;
  FILE *server;
  (void) arg;

  
  // make sure the server is initialized first.  
  // FIXME: cleaner to use a condition var
  thread_usleep(500); 

  output("    client - getting socket\n");
  checkreturn( ((sock = socket(PF_INET, SOCK_STREAM, 0)) == -1), "socket");
  output("    client - got socket\n");

  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port        = htons(port);
  output("    client - connecting to server\n");
  checkreturn( ((connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)), "connect");
  output("    client - connection establised\n");

  output("    client - reading\n");
  checkreturn( ((server = fdopen(sock,"r+")) == NULL), "fdopen");
  checkreturn( (ret = fgets(buf, sizeof(buf), server) == NULL), "fgets");
  output("    client - got %d bytes - '%s'\n",ret,buf);

  return NULL;
}



void* sockio_test()
{
  thread_t *server, *client;

  banner("sockio");

  server = thread_spawn("sockio_test_server", sockio_test_server, NULL);
  client = thread_spawn("sockio_test_client", sockio_test_client, NULL);

  thread_yield();
  thread_join(server, NULL);

  thread_join(client, NULL);
  //kill(getpid(), SIGUSR1);

  output("done.\n");
  return NULL;
}


//////////////////////////////////////////////////////////////////////
// disk IO testing routines
//////////////////////////////////////////////////////////////////////

//extern ssize_t __libc_read(int, void*, ssize_t);

void *diskio_test()
{
  int fd, ret;
  char buf[130]; // 3 lines of /etc/mime.types
  buf[ sizeof(buf)-1 ] = '\0';

  banner("diskio");
  output("    Read test.\n");
  output("    doing open .... \n");
  fd = open("/etc/mime.types",O_RDONLY);
  output("        fd=%d\n",fd);
  output("    reading...\n");
  ret = read(fd, buf, sizeof(buf)-1);
  if (ret < 0) {
    output("    error with read: %s\n",strerror(errno));
  } else {
    buf[ret] = '\0';
    output("    read data:\n");
    output("%s\n", buf);
  }
  close(fd);

  output("\n    Write test.\n");
  output("    doing open...\n");
  fd = open("__test", O_RDWR | O_CREAT, 0644);
  output("        fd=%d\n",fd);
  output("    writing to file __test...\n");
  ret = write(fd, buf, sizeof(buf)-1);
  if (ret < 0) {
    perror("Cannot write to disk file.");
  }
  
  close(fd);
  //output("    Content written:\n");
  //system("cat __test");
  //output("\n");
  unlink("__test");

  output("done.\n");
  
  return NULL;
}

// Use buffered I/O to test whether C library get linked to the mapped syscalls
void syscall_mapping_test()
{
  FILE *f;
  char buf[130]; // 3-lines of /etc/mime.types
  int c;

  banner("syscall mapping (buffered I/O)");

  output("    calling fopen()\n");
  f = fopen("/etc/mime.types", "r");
  if (!f) {
    perror("Cannot open file.");
    return;
  }

  output("    calling fread()\n");
  c = fread(buf, 1, sizeof(buf)-1, f);
  buf[ sizeof(buf)-1 ]=0;
  output("    Data read:\n");
  output("%s\n", buf);

  output("    calling fclose()\n");
  fclose(f);

  // verify libc overriding
  output("    calling printf()\n");
  printf("printf test: this should go through the aio library!!!!\n");

  return;
}

void system_test()
{
  banner("system()");
  output("    calling system('head -3 /etc/mime.types')\n");
  output("    NOTE: this will hose AIO, due to the fork() call!!\n");
  system("head -3 /etc/mime.types");
}

#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

void readdir_test() {
  DIR *dir;
  struct dirent *dent;

  banner("readdir()");
  output("    opening dir \n");
  dir = opendir("/usr"); 
  if (!dir) {
    output("    Cannot open dir.\n");
    return;
  }
  output("    read from the dir\n");
  dent = readdir(dir);
  output("    dir name: %s\n", dent ? dent->d_name : "NULL");
  closedir(dir);
}

void malloc_test() {
  void *a; 
  void *b; 

  banner("malloc()");
  
  a = malloc(20);
  b = malloc(13221);

  free(a);
  free(b);
}


int main(int argc, char *argv[])
{
  (void) argc;
  (void) argv;

  // NOTE: don't use unbuffered IO - it crashes the threads lib
  //setlinebuf(stdout);
  //setlinebuf(stderr);
  //  thread_init();

  // FIXME: doing a fork() seems to completely hose aio.  

  if( 0 ) {
    system_test();
    sockio_test();
    exit(0);
  }

  system_test();
  sockio_test();
  diskio_test();
  syscall_mapping_test();

  readdir_test();

  malloc_test();
  
//  malloc(10);
//  output("after malloc()\n");
//  strdup("abc");
//  output("after strdup()\n");

  output("DONE\n\n");

  if( 0 ) {
    sleep(2);
    output("sending USR1 to pid=%d\n", getpid());
    kill(getpid(), SIGUSR1);
    sleep(2);
    output("exiting\n");
  }

  return 0;
}

