#include "native_abi.h"
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#define TN_GPU_ABI
#else
#include <unistd.h>
#include <sched.h>
#include <dlfcn.h>
#if defined(__x86_64__)
#define TN_GPU_ABI __attribute__((ms_abi))
#else
#define TN_GPU_ABI
#endif
#endif

static void* gpu_module;
static atomic_int gpu_initialized;
static atomic_int gpu_reported;

static void* gpu_symbol(void* module, const char* name){
#ifdef _WIN32
  return (void*)GetProcAddress((HMODULE)module,name);
#else
  return dlsym(module,name);
#endif
}

static int gpu_library_path(char* path, size_t capacity){
#ifdef _WIN32
  DWORD length=GetModuleFileNameA(NULL,path,(DWORD)capacity);
  if(length==0 || length>=capacity) return 0;
  const char* library="tzgpu.dll";
#else
  ssize_t length=readlink("/proc/self/exe",path,capacity-1);
  if(length<=0 || (size_t)length>=capacity-1) return 0;
  path[length]=0;
  const char* library="libtzgpu-native.so";
#endif
  char* slash=strrchr(path,'/'); char* backslash=strrchr(path,'\\');
  if(backslash && (!slash || backslash>slash)) slash=backslash;
  if(!slash || (size_t)(slash-path)+1+strlen(library)>=capacity) return 0;
  strcpy(slash+1,library);
  return 1;
}

static void* gpu_backend(void){
  int expected=0;
  if(atomic_compare_exchange_strong(&gpu_initialized,&expected,1)){
    char path[4096];
    void* loaded=NULL;
    if(gpu_library_path(path,sizeof(path))){
#ifdef _WIN32
      loaded=(void*)LoadLibraryExA(path,NULL,LOAD_WITH_ALTERED_SEARCH_PATH);
#else
      loaded=dlopen(path,RTLD_NOW|RTLD_LOCAL);
#endif
    }
    if(loaded){
      typedef int64_t (TN_GPU_ABI *BackendKind)(void);
      BackendKind kind=(BackendKind)gpu_symbol(loaded,"tz_cuda_backend_kind");
      if(!kind || kind()!=1){
#ifdef _WIN32
        FreeLibrary((HMODULE)loaded);
#else
        dlclose(loaded);
#endif
        loaded=NULL;
      }
    }
    gpu_module=loaded;
    atomic_store(&gpu_initialized,loaded ? 2 : 3);
  } else {
    while(atomic_load(&gpu_initialized)==1){
#ifdef _WIN32
      Sleep(0);
#else
      sched_yield();
#endif
    }
  }
  return gpu_module;
}

static void gpu_unavailable(const char* symbol){
  if(atomic_exchange(&gpu_reported,1)==0){
    fprintf(stderr,"TezzNative: CUDA backend unavailable or incompatible while calling %s\n",symbol);
  }
}

#define TN_GPU_BIND(symbol, result_type, parameters, arguments, unavailable) \
  TN_NATIVE_API result_type tn_native_##symbol parameters { \
    typedef result_type (TN_GPU_ABI *Function) parameters; \
    void* module=gpu_backend(); \
    Function function=module ? (Function)gpu_symbol(module,#symbol) : NULL; \
    if(!function){ if((unavailable)<0) gpu_unavailable(#symbol); return (unavailable); } \
    return function arguments; \
  }
#include "gpu_bindings.h"
#undef TN_GPU_BIND
