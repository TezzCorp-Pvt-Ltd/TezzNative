#include "native_abi.h"
#include <stdlib.h>
#include <stdint.h>

TN_NATIVE_API void* tn_native_malloc(long long size){
  if(size<=0 || (unsigned long long)size>(unsigned long long)SIZE_MAX) return NULL;
  return malloc((size_t)size);
}

TN_NATIVE_API void tn_native_free(void* pointer){ free(pointer); }
