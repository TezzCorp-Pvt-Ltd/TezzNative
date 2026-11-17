#include "native_abi.h"
#include <stdio.h>
#include <string.h>
TN_NATIVE_API long long tn_args_count(void);
TN_NATIVE_API const char* tn_arg(long long index);
int main(void){
  long long count=tn_args_count();
  if(tn_arg(-1) || tn_arg(count)) return 1;
  printf("%lld\n",count);
  for(long long i=0;i<count;i++){
    const unsigned char* value=(const unsigned char*)tn_arg(i);
    if(!value) return 2;
    for(size_t j=0;j<strlen((const char*)value);j++) printf("%02x",value[j]);
    putchar('\n');
  }
  return 0;
}
