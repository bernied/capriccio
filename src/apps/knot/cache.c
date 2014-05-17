#include <pthread.h>

#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "cache.h"
#include "plhash.h"
#include "httphdrs.h"
#include "util.h"

#ifndef DEBUG_cache_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif


extern pthread_mutex_t g_cache_mutex;
extern int g_use_timer;
extern int g_cache_hits;
extern int g_cache_misses;

static PLHashTable *g_hash = NULL;
static cache_entry *g_cache = NULL;
static cache_entry *g_cache_tail = NULL;

static ssize_t g_cache_max = 512 * 1024 * 1024;
//static ssize_t g_cache_max = 256 * 1024 * 1024;
static ssize_t g_cache_cur = 0;

static
cache_entry *
cache_new(char *filename)
{
    cache_entry *result = NULL;
    int fd = open(filename, O_RDONLY);
    int ret;

    if (fd >= 0)
    {
        struct stat fd_stat;
        char *data = NULL;
        size_t length = 0;
        int hdrlen = 0;

        if (fstat(fd, &fd_stat) < 0)
        {
            perror("fstat");
            exit(1);
        }
        
        // make sure we have a regular file
        if ( !S_ISREG( fd_stat.st_mode ) )
        {
            return NULL;
        }

        length = fd_stat.st_size;

        data = malloc(length + HEADER_200_BUF_SIZE);
        assert(data != NULL);

        hdrlen = snprintf(data, HEADER_200_BUF_SIZE, HEADER_200,
                          "text/html", length);

        if (hdrlen < 0 || hdrlen >= HEADER_200_BUF_SIZE)
        {
            fprintf(stderr, "header buffer exceeded\n");
            exit(1);
        }

        ret = read(fd, data + hdrlen, length);
        if ( (size_t)ret != length )
        {
            perror("read");
            fprintf(stderr,"read failed - expected %d, but got %d\n",length, ret);
            close(fd);
            return NULL;
        }

        result = (cache_entry *) malloc(sizeof(*result));
        assert(result != NULL);

        result->filename = strdup(filename);
        assert(result->filename != NULL);

        result->data = data;
        result->total = length + hdrlen;

        result->refs = 1;
        pthread_mutex_init(&result->refs_mutex, NULL);

        result->next = NULL;
        result->prev = NULL;

        close(fd);
    }

    return result;
}

static
void
cache_add(cache_entry *entry)
{
    assert(entry != NULL);

    if (g_cache != NULL)
    {
        assert(entry->next == NULL);
        assert(entry->prev == NULL);
        assert(g_cache->prev == NULL);

        entry->next = g_cache;
        entry->prev = NULL;
        g_cache->prev = entry;
    }

    if (g_cache_tail == NULL)
    {
        g_cache_tail = entry;
    }

    g_cache = entry;
    g_cache_cur += entry->total;

    debug("cache head: %p tail: %p size: %d\n",
          g_cache, g_cache_tail, g_cache_cur);

    PL_HashTableAdd(g_hash, entry->filename, entry);
}

static
void
cache_remove(cache_entry *entry)
{
    assert(entry != NULL);

    if (g_cache == entry || entry->prev != NULL)
    {
        // if this entry is in the cache, update the cache size
        g_cache_cur -= entry->total;
    }

    if (g_cache == entry)
    {
        assert(g_cache->prev == NULL);
        g_cache = g_cache->next;
    }

    if (g_cache_tail == entry)
    {
        assert(g_cache_tail->next == NULL);
        g_cache_tail = g_cache_tail->prev;
    }

    if (entry->prev != NULL)
    {
        entry->prev->next = entry->next;
    }

    if (entry->next != NULL)
    {
        entry->next->prev = entry->prev;
    }

    entry->prev = NULL;
    entry->next = NULL;

    PL_HashTableRemove(g_hash, entry->filename);
}

static
void
cache_evict(void)
{
    while (g_cache_cur > g_cache_max)
    {
        cache_entry *remove = g_cache_tail;

        assert(remove != NULL);
        cache_remove(remove);

        cache_entry_release(remove);
    }
}

static
cache_entry *
cache_find(char *filename)
{
    return (cache_entry *) PL_HashTableLookup(g_hash, filename);
}

static
void
cache_use(cache_entry *entry)
{
    // move to front
    if (g_cache != entry)
    {
        cache_remove(entry);
        cache_add(entry);
    }
}

static
void
cache_finish_get(cache_entry *entry)
{
    cache_use(entry);
    cache_entry_addref(entry); // important: before evict!
    cache_evict();
}

void
cache_init(void)
{
    g_hash = PL_NewHashTable(
                 100, PL_HashString, PL_CompareStrings, PL_CompareValues,
                 NULL, NULL);
    assert(g_hash != NULL);
}

cache_entry *
cache_get(char *filename)
{
    cache_entry *result = NULL;

    // TODO: fine-grained locking
    pthread_mutex_lock(&g_cache_mutex);

    result = cache_find(filename);

    if (result != NULL)
    {
        g_cache_hits++;
        cache_finish_get(result);
    } 
    else
    {
        g_cache_misses++;
    }

    pthread_mutex_unlock(&g_cache_mutex);

    if (result == NULL)
    {
        debug("file [%s] not in cache; adding\n", filename);

        result = cache_new(filename);

        if (result != NULL)
        {
            pthread_mutex_lock(&g_cache_mutex);

            cache_add(result);
            cache_finish_get(result);

            pthread_mutex_unlock(&g_cache_mutex);
        }
    }
    else
    {
        debug("file [%s] in cache\n", filename);
    }

    return result;
}

void
cache_entry_addref(cache_entry *entry)
{
    if (entry != NULL)
    {
        pthread_mutex_lock(&entry->refs_mutex);
        entry->refs++;
        pthread_mutex_unlock(&entry->refs_mutex);
    }
}

void
cache_entry_release(cache_entry *entry)
{
    int refs = 0;

    if (entry != NULL)
    {
        pthread_mutex_lock(&entry->refs_mutex);
        entry->refs--;
        refs = entry->refs;
        pthread_mutex_unlock(&entry->refs_mutex);

        if (refs == 0)
        {
            pthread_mutex_destroy(&entry->refs_mutex);
            free(entry->filename);
            free(entry->data);
            free(entry);
            entry = NULL;
        }
    }
}


//////////////////////////////////////////////////
// Set the emacs indentation offset
// Local Variables: ***
// c-basic-offset:4 ***
// End: ***
//////////////////////////////////////////////////
