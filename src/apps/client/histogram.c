

#include <string.h>
#include "histogram.h"

void hist_init( histogram_t *h, int nbuckets, int shift )
{
  bzero(h, sizeof(histogram_t) + nbuckets*sizeof(int));
  h->nbuckets = nbuckets;
  h->shift = shift;
}

histogram_t* hist_new( int nbuckets, int shift )
{
  histogram_t *h = malloc( sizeof(histogram_t) + nbuckets*sizeof(int) );
  hist_init(h, nbuckets, shift);
  return h;
}



#ifdef TEST_MAIN

#include <stdio.h>
int main(int argc, char **argv)
{
  
  histogram_t *h = hist_new(100,2);

  hist_add(h,10);
  hist_add(h,100);
  hist_add(h,1000);
  hist_add(h,1);

  hist_add(h,1);
  hist_add(h,1);
  hist_add(h,5);
  hist_add(h,8);

  printf("num: %d   mean: %d\n", hist_num(h), hist_mean(h));
}


#endif
