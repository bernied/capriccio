#ifndef CACHE_H
#define CACHE_H

#include <pthread.h>
#include <sys/types.h>

typedef struct cache_entry cache_entry;
struct cache_entry
{
  char *filename;

  char *data;
  ssize_t total;
  
  int refs;
  pthread_mutex_t refs_mutex;
  
  cache_entry *next;
  cache_entry *prev;
};

void
cache_init(void);

cache_entry *
cache_get(char *filename);

void
cache_entry_addref(cache_entry *entry);

void
cache_entry_release(cache_entry *entry);

#endif // CACHE_H
