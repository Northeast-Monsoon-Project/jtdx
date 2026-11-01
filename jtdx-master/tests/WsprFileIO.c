/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdlib.h>
#include "lib/wsprd/WsprFileIO.h"
static void u16(unsigned char *p,unsigned x){p[0]=x;p[1]=x>>8;}
static void u32(unsigned char *p,unsigned x){u16(p,x);u16(p+2,x>>16);}
static size_t make(unsigned char *p,int bext){
    memset(p,0,128);memcpy(p,"RIFF",4);memcpy(p+8,"WAVE",4);
    size_t i=12;
    if(bext){memcpy(p+i,"bext",4);u32(p+i+4,3);memcpy(p+i+8,"xyz",3);i+=12;}
    memcpy(p+i,"fmt ",4);u32(p+i+4,16);u16(p+i+8,1);u16(p+i+10,1);u32(p+i+12,12000);u32(p+i+16,24000);u16(p+i+20,2);u16(p+i+22,16);i+=24;
    memcpy(p+i,"data",4);u32(p+i+4,6);u16(p+i+8,32767);u16(p+i+10,32768);u16(p+i+12,123);i+=14;u32(p+4,i-8);return i;
}
static int read_blob(unsigned char const *data,size_t size,short *out,size_t *n){FILE *f=tmpfile();assert(f);assert(fwrite(data,1,size,f)==size);rewind(f);int ok=fm_read_pcm12k(f,out,8,n);fclose(f);return ok;}
int main(void){
    unsigned char data[128];short out[8]={0};size_t n=0,size;
    for(int bwf=0;bwf<2;bwf++){size=make(data,bwf);assert(read_blob(data,size,out,&n));assert(n==3&&out[0]==32767&&out[1]==-32768&&out[2]==123);}
    size=make(data,0);assert(!read_blob(data,size-1,out,&n));
    size=make(data,0);u32(data+24,48000);assert(!read_blob(data,size,out,&n));
    size=make(data,0);u16(data+22,2);assert(!read_blob(data,size,out,&n));
    size=make(data,0);u32(data+40,5);assert(!read_blob(data,size,out,&n));
    size=make(data,1);u32(data+16,0xffffffff);assert(!read_blob(data,size,out,&n));
    size=make(data,0);u32(data+4,0xffffffff);assert(!read_blob(data,size,out,&n));
    for(size_t length=0;length<44;length++)assert(!read_blob(data,length,out,&n));
    char path[16];assert(fm_join_path(path,sizeof path,".","file"));assert(!fm_join_path(path,sizeof path,"long-directory-name","file"));
    assert(fm_extension_is("some.wav.folder/a.WAV",".wav"));assert(!fm_extension_is("some.wav.folder/a.c2",".wav"));assert(!fm_extension_is("a",".wav"));
    puts("WSPR RIFF/BWF, truncation, sample format and path boundary tests passed");return 0;
}
