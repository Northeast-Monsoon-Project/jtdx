/* GPL-3.0-or-later: exercise actual allocation failure, not just a mock decoder. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdlib.h>
#include <limits.h>
static int fail_allocation;
static void *test_malloc(size_t n){return fail_allocation ? NULL : malloc(n);}
#define malloc test_malloc
#include "lib/wsprd/fano.c"
#undef malloc
int main(void){
  unsigned metric=99,cycles=99,progress=99;
  unsigned char data[11]={0},symbols[162]={0};
  int table[2][256]={0};
  for(int i=0;i<256;i++){table[0][i]=255-i;table[1][i]=i;}
  assert(fano(&metric,&cycles,&progress,data,symbols,0,table,60,10)==-1);
  assert(fano(&metric,&cycles,&progress,data,symbols,31,table,60,10)==-1);
  assert(fano(&metric,&cycles,&progress,data,symbols,82,table,60,10)==-1);
  assert(fano(&metric,&cycles,&progress,data,symbols,81,table,0,10)==-1);
  assert(fano(&metric,&cycles,&progress,data,symbols,81,table,60,UINT_MAX)==-1);
  assert(fano(&metric,&cycles,&progress,data,symbols,81,NULL,60,10)==-1);
  assert(fano(NULL,&cycles,&progress,data,symbols,81,table,60,10)==-1);
  fail_allocation=1;
  assert(fano(&metric,&cycles,&progress,data,symbols,81,table,60,10)==-1);
  assert(metric==0 && cycles==0 && progress==0);
  fail_allocation=0;
  assert(fano(&metric,&cycles,&progress,data,symbols,81,table,60,10)==0);
  for(int i=0;i<10;i++)assert(data[i]==0);
  return 0;
}
