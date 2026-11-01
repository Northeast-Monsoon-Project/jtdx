/* JTDX-F bounded WSPR input helpers. GPL-3.0-or-later. */
#ifndef JTDXF_WSPR_FILE_IO_H
#define JTDXF_WSPR_FILE_IO_H
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
static uint16_t fm_u16(const unsigned char *b){return (uint16_t)(b[0]|((uint16_t)b[1]<<8));}
static uint32_t fm_u32(const unsigned char *b){return (uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);}
static int fm_extension_is(const char *path,const char *extension){
    size_t n=strlen(path),m=strlen(extension);
    if(n<m)return 0;
    for(size_t i=0;i<m;i++)if(tolower((unsigned char)path[n-m+i])!=tolower((unsigned char)extension[i]))return 0;
    return 1;
}
static int fm_join_path(char *out,size_t capacity,const char *directory,const char *name){
    int n=snprintf(out,capacity,"%s/%s",directory?directory:".",name);
    return n>=0 && (size_t)n<capacity;
}
/* Accept ordinary PCM WAV and BWF chunks. Reject truncation, unsupported
   encodings and sample rates before running a large FFT. Output is zero padded
   by the caller, permitting a deliberately short but complete recording. */
static int fm_read_pcm12k(FILE *f,short *samples,size_t capacity,size_t *count){
    unsigned char head[16];long length;int have_format=0;
    *count=0;
    if(fseek(f,0,SEEK_END) || (length=ftell(f))<12 || length>128*1024*1024 || fseek(f,0,SEEK_SET))return 0;
    if(fread(head,1,12,f)!=12 || memcmp(head,"RIFF",4) || memcmp(head+8,"WAVE",4))return 0;
    uint64_t riff_end=(uint64_t)fm_u32(head+4)+8;
    if(riff_end<12 || riff_end>(uint64_t)length)return 0;
    while((uint64_t)ftell(f)+8<=riff_end){
        if(fread(head,1,8,f)!=8)return 0;
        uint32_t size=fm_u32(head+4);long payload=ftell(f);
        uint64_t next=(uint64_t)payload+size+(size&1);
        if(next>riff_end || next>LONG_MAX)return 0;
        if(!memcmp(head,"fmt ",4)){
            if(size<16 || fread(head,1,16,f)!=16)return 0;
            uint16_t format=fm_u16(head);
            if(fm_u16(head+2)!=1 || fm_u32(head+4)!=12000 || fm_u32(head+8)!=24000 || fm_u16(head+12)!=2 || fm_u16(head+14)!=16)return 0;
            if(format==0xfffe){
                unsigned char extension[24];
                static const unsigned char pcm_guid[16]={1,0,0,0,0,0,16,0,128,0,0,170,0,56,155,113};
                if(size<40 || fread(extension,1,24,f)!=24 || fm_u16(extension)<22 || fm_u16(extension+2)!=16 || memcmp(extension+8,pcm_guid,16))return 0;
            }else if(format!=1)return 0;
            have_format=1;
        }else if(!memcmp(head,"data",4)){
            if(!have_format || size<2 || size%2)return 0;
            size_t n=size/2;if(n>capacity)n=capacity;
            if(fread(samples,2,n,f)!=n)return 0;
            unsigned char *raw=(unsigned char*)samples;
            for(size_t i=0;i<n;i++)samples[i]=(int16_t)fm_u16(raw+2*i);
            *count=n;return n>0;
        }
        if(fseek(f,(long)next,SEEK_SET))return 0;
    }
    return 0;
}
#endif
