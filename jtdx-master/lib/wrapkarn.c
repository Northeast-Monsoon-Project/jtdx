#include <math.h>
#include <stdio.h>
#include <float.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "ftrsd/int.h"

static void *rs;
static atomic_flag init_lock=ATOMIC_FLAG_INIT;
static void *jt65_codec(void) {
  while(atomic_flag_test_and_set_explicit(&init_lock,memory_order_acquire)){}
  if(!rs)rs=init_rs_int(6,0x43,3,1,51);
  void *result=rs;
  atomic_flag_clear_explicit(&init_lock,memory_order_release);
  return result;
}

void rs_encode_(int *dgen, int *sent)
// Encode JT65 data dgen[12], producing sent[63].
{
  int dat1[12];
  int b[51];
  int i;

  if(!sent)return;
  memset(sent,0,63*sizeof *sent);
  if(!dgen)return;
  for(i=0;i<12;i++)if(dgen[i]<0 || dgen[i]>63)return;
  void *codec=jt65_codec();if(!codec)return;

  // Reverse data order for the Karn codec.
  for(i=0; i<12; i++) {
    dat1[i]=dgen[11-i];
  }
  // Compute the parity symbols
  encode_rs_int(codec,dat1,b);

  // Move parity symbols and data into sent[] array, in reverse order.
  for (i = 0; i < 51; i++) sent[50-i] = b[i];
  for (i = 0; i < 12; i++) sent[i+51] = dat1[11-i];
}

void rs_decode_(int *recd0, int *era0, int *numera0, int *decoded, int *nerr)
// Decode JT65 received data recd0[63], producing decoded[12].
// Erasures are indicated in era0[numera].  The number of corrected
// errors is *nerr.  If the data are uncorrectable, *nerr=-1 is returned.
{
  int numera;
  int i;
  int era_pos[51];
  int recd[63];

  if(!nerr)return;
  *nerr=-1;
  if(!decoded)return;
  memset(decoded,0,12*sizeof *decoded);
  if(!recd0 || !numera0)return;
  numera=*numera0;
  if(numera<0 || numera>51 || (numera && !era0))return;
  void *codec=jt65_codec();if(!codec)return;
  for(i=0; i<12; i++) recd[i]=recd0[62-i];
  for(i=0; i<51; i++) recd[12+i]=recd0[50-i];
  if(numera) 
    for(i=0; i<numera; i++) era_pos[i]=era0[i];
  *nerr=decode_rs_int(codec,recd,era_pos,numera,1);
  for(i=0; i<12; i++) decoded[i]=recd[11-i];
}


void rs_encode__(int *dgen, int *sent)
{
  rs_encode_(dgen, sent);
}

void rs_decode__(int *recd0, int *era0, int *numera0, int *decoded, int *nerr)
{
  rs_decode_(recd0, era0, numera0, decoded, nerr);
}

