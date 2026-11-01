#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include "lib/wsprd/jelinek.h"
int main(void) {
  unsigned metric=0,cycles=0;
  unsigned char data[11]={0},symbols[162]={0};
  int metrics[2][256];
  struct snode stack[1000];
  for(int j=0;j<256;++j) { metrics[0][j]=j-128; metrics[1][j]=127-j; }
  assert(jelinek(&metric,&cycles,data,symbols,82,1000,stack,metrics,10)==-1);
  assert(jelinek(&metric,&cycles,data,symbols,0,1000,stack,metrics,10)==-1);
  for(int test=0;test<200;++test) {
    for(int i=0;i<162;++i) symbols[i]=(unsigned char)((test*17+i*31)&255);
    jelinek(&metric,&cycles,data,symbols,81,1000,stack,metrics,20);
  }
  return 0;
}
