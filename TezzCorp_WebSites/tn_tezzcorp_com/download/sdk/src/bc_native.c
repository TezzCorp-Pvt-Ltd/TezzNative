#include "bc_native.h"
#include "native_imports.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#if !defined(_WIN32) && defined(__x86_64__)
#define TN_VM_ABI __attribute__((ms_abi))
#else
#define TN_VM_ABI
#endif

static int64_t tn_vm_integer(Value v){
  return v.k==V_PTR ? (int64_t)(intptr_t)v.p : v.i;
}

static double tn_vm_double(Value v){
  return v.k==V_F64 ? v.f : (double)tn_vm_integer(v);
}

static float tn_vm_float(Value v){
  if(v.k==V_F64) return (float)v.f;
  uint32_t bits=(uint32_t)v.i;
  float result;
  memcpy(&result,&bits,sizeof(result));
  return result;
}

static void* tn_vm_symbol(int library, const char* symbol){
  static void* modules[TN_NATIVE_LIBRARY_COUNT];
  char bridge_symbol[160];
  if(library==TN_NATIVE_GPU){
    snprintf(bridge_symbol,sizeof(bridge_symbol),"tn_native_%s",symbol);
    symbol=bridge_symbol;
    library=TN_NATIVE_RUNTIME;
  }
#ifdef _WIN32
  const int windows=1;
#else
  const int windows=0;
#endif
  if(!modules[library]){
    char path[4096];
    const char* name=tn_native_library_name(library,windows);
    if(tn_native_library_path(name,path,sizeof(path))<0){
      fprintf(stderr,"tezzc error: BC-VM: required native library '%s' is missing\n",name);
      exit(1);
    }
#ifdef _WIN32
    modules[library]=(void*)LoadLibraryExA(path,NULL,LOAD_WITH_ALTERED_SEARCH_PATH);
#else
    modules[library]=dlopen(path,RTLD_NOW|RTLD_LOCAL);
#endif
    if(!modules[library]){
      fprintf(stderr,"tezzc error: BC-VM: cannot load native library '%s'\n",path);
      exit(1);
    }
  }
#ifdef _WIN32
  void* function=(void*)GetProcAddress((HMODULE)modules[library],symbol);
#else
  void* function=dlsym(modules[library],symbol);
#endif
  if(!function){
    fprintf(stderr,"tezzc error: BC-VM: native library does not export '%s'\n",symbol);
    exit(1);
  }
  return function;
}

int tn_vm_native_call(const char* name, int len, Value* args, int argc, Value* out){
  for(int i=len-1;i>=0;i--){
    if(name[i]=='.'){ name+=i+1; len-=i+1; break; }
  }
  if(len==14 && !memcmp(name,"tn_decimal_f64",14)){
    typedef long long (TN_VM_ABI *Function)(const unsigned char*,long long,double*);
    if(argc!=3){ fprintf(stderr,"BC-VM: decimal signature mismatch\n"); exit(1); }
    Function function=(Function)tn_vm_symbol(TN_NATIVE_RUNTIME,"tn_decimal_f64");
    memset(out,0,sizeof(*out)); out->k=V_I64;
    out->i=function((const unsigned char*)(intptr_t)tn_vm_integer(args[0]),
                   tn_vm_integer(args[1]),(double*)(intptr_t)tn_vm_integer(args[2]));
    return 1;
  }
  if(len>12 && !memcmp(name,"tn_security_",12)){
#define TN_VM_SECURITY(symbol, result_type, arity, parameters, values, pointer_result) \
    if(len==(int)sizeof(#symbol)-1 && !memcmp(name,#symbol,(size_t)len)){ \
      typedef result_type (TN_VM_ABI *Function) parameters; \
      if(argc!=arity){ fprintf(stderr,"BC-VM: security signature mismatch\n"); exit(1); } \
      Function function=(Function)tn_vm_symbol(TN_NATIVE_RUNTIME,#symbol); \
      intptr_t result=(intptr_t)function values; \
      memset(out,0,sizeof(*out)); out->i=(int64_t)result; \
      out->k=pointer_result ? V_PTR : V_I64; \
      if(pointer_result) out->p=(void*)result; \
      return 1; \
    }
    TN_VM_SECURITY(tn_security_password_hash, unsigned char*, 1, (const unsigned char*),
                   ((const unsigned char*)(intptr_t)tn_vm_integer(args[0])), 1)
    TN_VM_SECURITY(tn_security_password_verify, long long, 2,
                   (const unsigned char*,const unsigned char*),
                   ((const unsigned char*)(intptr_t)tn_vm_integer(args[0]),
                    (const unsigned char*)(intptr_t)tn_vm_integer(args[1])), 0)
    TN_VM_SECURITY(tn_security_random_token, unsigned char*, 0, (void), (), 1)
    TN_VM_SECURITY(tn_security_token_hash, unsigned char*, 1, (const unsigned char*),
                   ((const unsigned char*)(intptr_t)tn_vm_integer(args[0])), 1)
    TN_VM_SECURITY(tn_security_public_path, unsigned char*, 2,
                   (const unsigned char*,const unsigned char*),
                   ((const unsigned char*)(intptr_t)tn_vm_integer(args[0]),
                    (const unsigned char*)(intptr_t)tn_vm_integer(args[1])), 1)
    TN_VM_SECURITY(tn_security_otp_secret, unsigned char*, 0, (void), (), 1)
    TN_VM_SECURITY(tn_security_otp_numeric, unsigned char*, 1, (long long),
                   (tn_vm_integer(args[0])), 1)
    TN_VM_SECURITY(tn_security_otp_random_u32, long long, 0, (void), (), 0)
    TN_VM_SECURITY(tn_security_otp_hotp, unsigned char*, 3,
                   (const unsigned char*,long long,long long),
                   ((const unsigned char*)(intptr_t)tn_vm_integer(args[0]),
                    tn_vm_integer(args[1]),tn_vm_integer(args[2])), 1)
    TN_VM_SECURITY(tn_security_otp_match, long long, 5,
                   (const unsigned char*,const unsigned char*,long long,long long,long long),
                   ((const unsigned char*)(intptr_t)tn_vm_integer(args[0]),
                    (const unsigned char*)(intptr_t)tn_vm_integer(args[1]),
                    tn_vm_integer(args[2]),tn_vm_integer(args[3]),tn_vm_integer(args[4])), 0)
    TN_VM_SECURITY(tn_security_otp_uri, unsigned char*, 3,
                   (const unsigned char*,const unsigned char*,const unsigned char*),
                   ((const unsigned char*)(intptr_t)tn_vm_integer(args[0]),
                    (const unsigned char*)(intptr_t)tn_vm_integer(args[1]),
                    (const unsigned char*)(intptr_t)tn_vm_integer(args[2])), 1)
#undef TN_VM_SECURITY
    return 0;
  }
  if(len>7 && !memcmp(name,"tn_npu_",7)){
#define TN_NPU_API(symbol, result_type, arity, parameters, values, pointer_result) \
    if(len==(int)sizeof(#symbol)-1 && !memcmp(name,#symbol,(size_t)len)){ \
      typedef result_type (TN_VM_ABI *Function) parameters; \
      if(argc!=arity){ fprintf(stderr,"BC-VM: NPU signature mismatch\n"); exit(1); } \
      Function function=(Function)tn_vm_symbol(TN_NATIVE_RUNTIME,#symbol); \
      intptr_t result=(intptr_t)function values; \
      memset(out,0,sizeof(*out)); out->i=(int64_t)result; \
      out->k=pointer_result ? V_PTR : V_I64; \
      if(pointer_result) out->p=(void*)result; \
      return 1; \
    }
#include "npu_api.h"
#undef TN_NPU_API
    return 0;
  }
  if(len<7 || (memcmp(name,"tz_cpu_",7) && memcmp(name,"tz_gpu_",7) &&
      (len<8 || memcmp(name,"tz_cuda_",8)))) return 0;
#define TN_VM_BIND(library, symbol, result_type, arity, parameters, values) \
  if(len==(int)sizeof(#symbol)-1 && !memcmp(name,#symbol,(size_t)len)){ \
    typedef result_type (TN_VM_ABI *Function) parameters; \
    static Function function; \
    if(argc!=arity){ \
      fprintf(stderr,"tezzc error: BC-VM: native signature mismatch for '%s'\n",#symbol); \
      exit(1); \
    } \
    if(!function) function=(Function)tn_vm_symbol(library,#symbol); \
    memset(out,0,sizeof(*out)); out->k=V_I64; out->i=function values; \
    return 1; \
  }
#include "bc_native_bindings.h"
#undef TN_VM_BIND
  return 0;
}
