#include "native_abi.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <stdio.h>
#endif

/* Generated executables have no CRT argv startup. Preserve the VM contract:
 * arg(0) is the first user argument, excluding the executable name. */
static char** arguments;
static long long argument_count;
static atomic_int initialized;

static int append_argument(char* value){
  if(argument_count >= 65536){ free(value); return -1; }
  char** next=(char**)realloc(arguments,(size_t)(argument_count+1)*sizeof(char*));
  if(!next){ free(value); return -1; }
  arguments=next; arguments[argument_count++]=value; return 0;
}

static void initialize_arguments(void){
#ifdef _WIN32
  const wchar_t* command=GetCommandLineW();
  size_t length=wcslen(command), position=0;
  int executable=1;
  while(position<length){
    while(command[position]==L' ' || command[position]==L'\t') position++;
    if(!command[position]) break;
    wchar_t* word=(wchar_t*)malloc((length-position+1)*sizeof(wchar_t));
    if(!word) break;
    size_t count=0; int quoted=0;
    while(position<length){
      wchar_t ch=command[position];
      if(!quoted && (ch==L' ' || ch==L'\t')) break;
      size_t slashes=0;
      while(command[position]==L'\\'){ slashes++; position++; }
      if(command[position]==L'"'){
        for(size_t i=0;i<slashes/2;i++) word[count++]=L'\\';
        if(slashes&1u){ word[count++]=L'"'; position++; }
        else if(quoted && command[position+1]==L'"'){ word[count++]=L'"'; position+=2; }
        else { quoted=!quoted; position++; }
      } else {
        while(slashes--) word[count++]=L'\\';
        if(position>=length) break;
        if(!quoted && (command[position]==L' ' || command[position]==L'\t')) break;
        word[count++]=command[position++];
      }
    }
    word[count]=0;
    int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,word,-1,NULL,0,NULL,NULL);
    char* value=bytes>0 ? (char*)malloc((size_t)bytes) : NULL;
    if(value && !WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,word,-1,value,bytes,NULL,NULL)){ free(value); value=NULL; }
    free(word);
    if(!value) break;
    if(executable){ executable=0; free(value); }
    else if(append_argument(value)) break;
  }
#else
  FILE* file=fopen("/proc/self/cmdline","rb");
  if(!file) return;
  size_t capacity=4096, used=0;
  char* data=(char*)malloc(capacity);
  if(!data){ fclose(file); return; }
  int ch;
  while((ch=fgetc(file))!=EOF && used<8388608){
    if(used==capacity){
      capacity*=2; char* next=(char*)realloc(data,capacity);
      if(!next){ free(data); fclose(file); return; } data=next;
    }
    data[used++]=(char)ch;
  }
  int failed=ferror(file) || ch!=EOF; fclose(file);
  if(failed || !used || data[used-1]){ free(data); return; }
  size_t start=0; int executable=1;
  for(size_t i=0;i<used;i++) if(!data[i]){
    if(executable) executable=0;
    else { char* value=(char*)malloc(i-start+1); if(!value) break;
      memcpy(value,data+start,i-start+1); if(append_argument(value)) break; }
    start=i+1;
  }
  free(data);
#endif
}

static void ensure_arguments(void){
  int expected=0;
  if(atomic_compare_exchange_strong(&initialized,&expected,1)){
    initialize_arguments(); atomic_store(&initialized,2);
  } else while(atomic_load(&initialized)!=2){}
}

TN_NATIVE_API long long tn_args_count(void){ ensure_arguments(); return argument_count; }
TN_NATIVE_API const char* tn_arg(long long index){
  ensure_arguments(); return index>=0 && index<argument_count ? arguments[index] : NULL;
}
