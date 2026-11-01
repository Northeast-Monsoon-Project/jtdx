/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include "lib/ftrsd/int.h"
void rs_encode_(int*,int*);
void rs_decode_(int*,int*,int*,int*,int*);
void ftrsd2_(int*,int*,int*,int*,int*,int*,int*,int*);
void getpp_(int *word,float *power){(void)word;*power=1.0f;}
static void *worker(void *arg){
  int base=*(int*)arg;
  for(int trial=0;trial<200;trial++){
    int input[12],code[63],out[12],eras[51],nerr,n=0;
    for(int j=0;j<12;j++)input[j]=(base+trial+j*3)%64;
    rs_encode_(input,code);
    rs_decode_(code,eras,&n,out,&nerr);assert(nerr==0 && !memcmp(input,out,sizeof input));
    for(int j=0;j<20;j++)code[j]^=7;
    rs_decode_(code,eras,&n,out,&nerr);assert(nerr==20 && !memcmp(input,out,sizeof input));
  }
  return NULL;
}
int main(void){
  pthread_t threads[4];int starts[4]={1,7,13,31};
  for(int i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,worker,&starts[i]));
  for(int i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));
  int input[12]={0},code[63],out[12],eras[51],nerr,n=51;
  rs_encode_(input,code);for(int i=0;i<51;i++)eras[i]=i;
  rs_decode_(code,eras,&n,out,&nerr);assert(nerr==0);
  n=52;rs_decode_(code,eras,&n,out,&nerr);assert(nerr==-1);
  n=1;eras[0]=63;rs_decode_(code,eras,&n,out,&nerr);assert(nerr==-1);
  n=0;code[0]=64;rs_decode_(code,eras,&n,out,&nerr);assert(nerr==-1);
  int second[63]={0},prob[63],prob2[63],correct[63],param[6],trials=0,pass=1;
  for(int j=0;j<63;j++){prob[j]=100;prob2[j]=50;}
  // Repeated successful paths previously leaked every allocated RS table.
  rs_encode_(input,code);
  for(int t=0;t<1000;t++){ftrsd2_(code,prob,second,prob2,&trials,&pass,correct,param);assert(param[2]==0);}
  for(int j=0;j<63;j++)code[j]=(j*17+11)%64;
  ftrsd2_(code,prob,second,prob2,&trials,&pass,correct,param);assert(param[2]==-1 && param[4]==0);
  prob2[0]=255;ftrsd2_(code,prob,second,prob2,&trials,&pass,correct,param);assert(param[2]==-1);
  return 0;
}
