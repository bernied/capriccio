
#ifndef HISTOGRAM_H

#include <stdlib.h>


typedef struct {
  int nbuckets;
  int overflow;
  int shift;

  int nvals;
  long long sum;

  int buckets[0]; // always at the end
} histogram_t;


void hist_init( histogram_t *h, int nbuckets, int shift );
histogram_t* hist_new( int nbuckets, int shift );


static inline void hist_add( histogram_t *h, int val ) 
{
  val = val >> h->shift;

  if( val < h->nbuckets )
    h->buckets[val]++;
  else 
    h->overflow++;

  h->nvals++;
  h->sum += val;
}

static inline int hist_mean( histogram_t *h ) 
{
  if( h->nvals == 0 ) 
    return 0;
  else
    return ((int) (h->sum / h->nvals)) << h->shift;
}

static inline int hist_num( histogram_t *h )
{
  return h->nvals;
}

static inline void hist_reset( histogram_t *h )
{
  hist_init(h, h->nbuckets, h->shift);
}

#endif //HISTOGRAM_H
