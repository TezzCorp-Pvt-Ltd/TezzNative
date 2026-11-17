#include <stdlib.h>
#include <string.h>
#include "native_abi.h"
#define TN_EXPORT TN_NATIVE_API
static unsigned char* tn_dup_cstr(const char* text){
  size_t n = strlen(text)+1;
  unsigned char* result = (unsigned char*)malloc(n);
  if(result) memcpy(result, text, n);
  return result;
}
#include "runtime/npu/hailo.h"
#include "runtime/npu/raw_unsupported.h"
