/* Build the production adapter independently of the compiler for ABI tests. */
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define TN_EXPORT __declspec(dllexport)
#else
#define TN_EXPORT __attribute__((visibility("default")))
#endif
static unsigned char* tn_dup_cstr(const char* value){
  size_t size = strlen(value) + 1;
  unsigned char* copy = (unsigned char*)malloc(size);
  if(copy) memcpy(copy, value, size);
  return copy;
}
#include "runtime/npu/openvino.h"
#include "runtime/npu/raw_unsupported.h"
