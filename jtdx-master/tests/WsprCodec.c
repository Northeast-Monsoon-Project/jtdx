/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lib/wsprd/wsprsim_utils.h"
#include "lib/wsprd/wsprd_utils.h"
int printdata=0;
int main(void) {
  static char calls[32768*13], grids[32768*5];
  unsigned char symbols[162];
  // Hash partial words must not read past exact-sized allocations. Check every
  // alignment against a padded reference, including lengths not divisible by 4.
  for(size_t length=1;length<=64;length++) {
    unsigned char reference[68];
    for(size_t j=0;j<sizeof reference;j++)reference[j]=(unsigned char)(j*17+3);
    unsigned expected=nhash(reference,length,146);
    for(size_t offset=0;offset<4;offset++) {
      unsigned char *allocation=malloc(length+offset);assert(allocation);
      memcpy(allocation+offset,reference,length);
      assert(nhash(allocation+offset,length,146)==expected);free(allocation);
    }
  }
  char *valid[]={"K1ABC FN42 33","PJ4/K1ABC 33","<PJ4/K1ABC> FK52UD 33","<K1ABC> FN42 33"};
  for(unsigned i=0;i<sizeof valid/sizeof *valid;++i) {
    assert(get_wspr_channel_symbols(valid[i],calls,grids,symbols));
    for(int j=0;j<162;++j)assert(symbols[j]<=3);
  }
  // A malformed persistent hash slot must not overflow a 13-byte callsign.
  unsigned hash=nhash("K1ABC",5,146);
  memset(calls+hash*13,'X',13);
  assert(get_wspr_channel_symbols("<K1ABC> FN42 33",calls,grids,symbols));
  memcpy(calls+hash*13,"ABCDEFGHIJKL",13);
  assert(get_wspr_channel_symbols("<K1ABC> FN42 33",calls,grids,symbols));
  char *invalid[]={"K1ABC","K1ABC FN42","PJ4/K1ABC","K1ABC/ 33","ABCDEFGHIJK/1 33",
    "K1ABC ZZZZ 33","K1ABC FN4222222222 33","K1ABC FN42 99999999999999","", "A/B/C 33"};
  for(unsigned i=0;i<sizeof invalid/sizeof *invalid;++i)assert(!get_wspr_channel_symbols(invalid[i],calls,grids,symbols));
  unsigned random=12345;char message[24];char alphabet[]="ABCDE0123456789 /<>";
  for(int sample=0;sample<10000;++sample) {
    int length=sample%24;
    for(int j=0;j<length;++j){random=random*1664525u+1013904223u;message[j]=alphabet[random%(sizeof alphabet-1)];}
    message[length]=0;get_wspr_channel_symbols(message,calls,grids,symbols);
  }
  puts("WSPR source codec: valid frames, malformed input and 10000 bounded fuzz cases passed");
  return 0;
}
