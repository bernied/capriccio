
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <assert.h>

#define MB 1024*1024

int main(int argc, char **argv) 
{
  int size = atoi(argv[1]);
  char *data;
  (void) argc;

  printf("allocating %d MB\n", size);
  size *= MB;

  data = (char*) malloc(size);
  assert(data);

  printf("locking pages\n");
  assert( mlock(data, size) == 0 );

  if( 0 ) {
    char *p;
    printf("touching pages\n");
    for(p=data; p<data+size; p += 4096) {
      printf("%p  %p\n", p, data);
      (*p)++;
    }
  }

  printf("sleeping\n");
  sleep(100000);

  return 0;
}
