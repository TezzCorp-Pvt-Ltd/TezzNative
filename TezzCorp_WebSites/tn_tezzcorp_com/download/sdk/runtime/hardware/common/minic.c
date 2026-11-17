#include "hardware.h"

void* memcpy(void* destination, const void* source, size_t length){
  unsigned char* out=destination;
  const unsigned char* in=source;
  for(size_t i=0;i<length;i++) out[i]=in[i];
  return destination;
}

void* memmove(void* destination, const void* source, size_t length){
  unsigned char* out=destination;
  const unsigned char* in=source;
  if((uintptr_t)out<(uintptr_t)in){
    for(size_t i=0;i<length;i++) out[i]=in[i];
  }else if(out!=in){
    for(size_t i=length;i;i--) out[i-1]=in[i-1];
  }
  return destination;
}

void* memset(void* destination, int value, size_t length){
  unsigned char* out=destination;
  for(size_t i=0;i<length;i++) out[i]=(unsigned char)value;
  return destination;
}

int memcmp(const void* left, const void* right, size_t length){
  const unsigned char* a=left;
  const unsigned char* b=right;
  for(size_t i=0;i<length;i++) if(a[i]!=b[i]) return a[i]<b[i] ? -1 : 1;
  return 0;
}

int64_t tn_strcmp(int64_t left, int64_t right){
  const unsigned char* a=(const unsigned char*)(uintptr_t)left;
  const unsigned char* b=(const unsigned char*)(uintptr_t)right;
  if(!a || !b) return a==b ? 0 : (a ? 1 : -1);
  while(*a && *a==*b){ a++; b++; }
  return *a<*b ? -1 : (*a>*b ? 1 : 0);
}

_Noreturn void abort(void){ tn_hw_finish(127); }
