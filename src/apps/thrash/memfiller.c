
#include <pthread.h>
#include <unistd.h>
#include "util.h"

#ifndef DEBUG_memfiller_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif


#define PAGE_SIZE   4096
//#define CHUNK_SIZE  (PAGE_SIZE * 128)
#define CHUNK_SIZE  (PAGE_SIZE * 16)
#define MAX_CHUNKS 30000  // much more than can fit in memory


int num_chunks = 0;
char *chunks[MAX_CHUNKS];

int tasks = 0;
int dtasks = 0;


pthread_mutex_t lock;


extern int vm_overload;

// just keep allocating chunks
void* allocator(void* arg)
{
  int i;
  (void) arg;


  while( 1 ) {

    while( vm_overload ) {
      sched_yield();
    }

    pthread_mutex_lock( &lock );

    // allocate some more
    if( num_chunks < MAX_CHUNKS ) {
      chunks[num_chunks] = malloc( CHUNK_SIZE );
      // FIXME: awful race here.
      if( chunks[num_chunks] )  num_chunks++;
      else                      perror("malloc");
      //output("allocator -- allocated chunk %d\n",num_chunks);

      // touch the pages of the newly allocated chunk
    }

    // touch a bunch of chunks at random, to keep things in memory
    if( num_chunks > 0 ) {
      for( i=0; i<100; i++ ) {
        chunks[ rand() % num_chunks ][ rand() % CHUNK_SIZE ] += 10;
      }
    }
 
    // yield
    tasks++;
    pthread_mutex_unlock( &lock );

    sched_yield();
  }

}

// deallocate random chunks
void* deallocator(void* arg)
{
  int num;
  void *chunk_to_free;
  (void) arg;

  while( 1 ) {

    pthread_mutex_lock( &lock );
    if( num_chunks > 0 ) {
      // free a random chunk, so we don't favor memory that hasn't been touched 
      num = rand() % num_chunks;
      chunk_to_free = chunks[num];
      
      // plug the hole
      num_chunks--;
      chunks[num] = chunks[num_chunks];
      //output("deallocator -- freed chunk %d\n",num);

      // free the memory after a yield, so we have a more interesting graph
      //sched_yield();
      free( chunk_to_free );
    }

    dtasks++;
    pthread_mutex_unlock( &lock );

    sched_yield();
  }
  
}


int main(int argc, char **argv)
{
  int a, d;


  if( argc != 3  ||  (a=atoi(argv[1])) < 0  ||  (d=atoi(argv[2])) < 0 ) {
    fprintf(stderr, "Format: memfiller  NUM_ALLOCATORS  NUM_DEALLOCATORS\n");
    exit(1);
  }

  srand(0);

  // init the mutex
  pthread_mutex_init( &lock, NULL );

  // spawn client threads
  {
    int rv;
    pthread_attr_t attr;
    pthread_t thread;
    int i;

    pthread_attr_init( &attr );
    rv = pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
    assert (rv == 0);

    for( i=0; i<d; i++ ) {
      rv = pthread_create(&thread, &attr, &deallocator, NULL);
      assert (rv == 0);
    }
    for( i=0; i<a; i++ ) {
      rv = pthread_create(&thread, &attr, &allocator, NULL);
      assert (rv == 0);
    }
  }
  

  // print stats
  {
    unsigned long long then, now=current_usecs();

    while( 1 ) {
      then = now;
      sleep( 1 );
      now = current_usecs();
      if(now == then) now = then+1;
      printf("%3d chunks   %5.2f secs    %.0f alloc/sec   %.0f dealloc/sec    vm_overload=%d\n", 
             num_chunks, (double)(now-then)/1e6, 
             (double)tasks*1e6/(now-then), (double)dtasks*1e6/(now-then),
             vm_overload
             );
      tasks = 0;
      dtasks = 0;
    }
  }  

  return 0;
}

