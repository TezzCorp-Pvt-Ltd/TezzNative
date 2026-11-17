#include "native_imports.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#include <dlfcn.h>
#endif

static const struct { const char* name; const char* symbol; } runtime_names[] = {
#define TN_NPU_API(symbol, result_type, arity, parameters, values, pointer_result) {#symbol, #symbol},
#include "npu_api.h"
#undef TN_NPU_API
  {"tn_decimal_f64", "tn_decimal_f64"},
  {"sys_exit", "tn_sys_exit"},
  {"args_count", "tn_args_count"}, {"arg", "tn_arg"},
  {"time_now", "tn_time_now"}, {"time_now_ms", "tn_time_now_ms"},
  {"time_now_ns", "tn_time_now_ns"}, {"sleep_ms", "tn_sleep_ms"},
  {"date_now", "tn_date_now"}, {"date_now_utc", "tn_date_now_utc"},
  {"os_name", "tn_os_name"},
  {"path_join", "tn_path_join"}, {"path_sep", "tn_path_sep"},
  {"path_basename", "tn_path_basename"}, {"path_dirname", "tn_path_dirname"},
  {"path_normalize", "tn_path_normalize"}, {"path_normalize_opts", "tn_path_normalize_opts"},
  {"get_env", "tn_getenv_str"},
  {"get_cwd", "tn_get_cwd"},
  {"list_dir", "tn_list_dir"},
  {"list_dir_recursive", "tn_list_dir_recursive"}, {"glob", "tn_glob"},
  {"fopen", "tn_fopen"}, {"fclose", "tn_fclose"},
  {"fread", "tn_fread"}, {"fwrite", "tn_fwrite"},
  {"fseek", "tn_fseek"}, {"ftell", "tn_ftell"}, {"fflush", "tn_fflush"},
  {"read_file", "tn_read_file"}, {"read_line", "tn_read_line"},
  {"read_bytes", "tn_read_bytes"}, {"write_line", "tn_write_line"},
  {"io_err", "tn_io_err"}, {"io_eof", "tn_io_eof"},
  {"stdin", "tn_stdin"}, {"stdout", "tn_stdout"}, {"stderr", "tn_stderr"},
  {"write_file", "tn_write_file"},
  {"file_size", "tn_file_size"},
  {"color_is_tty_stdout", "tn_color_is_tty_stdout"},
  {"tts_play_pcm", "tn_tts_play_pcm"},
  {"stt_capture_mic", "tn_stt_capture_mic"},
  {"os_system", "tn_os_system"},
  {"mmap_file", "tn_mmap_file"},
  {"munmap_file", "tn_munmap_file"},
  {"io_mmap_open", "tn_mmap_file"},
  {"io_mmap_close", "tn_munmap_file"},
  {"io_pipe_create", "tn_io_pipe_create"},
  {"io_fd_read", "tn_io_fd_read"},
  {"io_fd_write", "tn_io_fd_write"},
  {"io_fd_close", "tn_io_fd_close"},
  {"fb_addr", "tn_fb_addr"}, {"fb_width", "tn_fb_width"},
  {"fb_height", "tn_fb_height"}, {"fb_pitch", "tn_fb_pitch"},
  {"fb_bpp", "tn_fb_bpp"}, {"screen_width", "tn_screen_width"},
  {"screen_height", "tn_screen_height"}, {"fb_init_ex", "tn_fb_init_ex"},
  {"fb_present", "tn_fb_present"}, {"fb_fill", "tn_fb_fill"},
  {"fb_fill_rect", "tn_fb_fill_rect"}, {"fb_put_pixel", "tn_fb_put_pixel"},
  {"fb_fill_rounded", "tn_fb_fill_rounded"}, {"fb_soft_cursor", "tn_fb_soft_cursor"},
  {"fb_text", "tn_fb_text"}, {"fb_text_ex", "tn_fb_text_ex"},
  {"kbd_has_event", "tn_kbd_has_event"}, {"kbd_read_char", "tn_kbd_read_char"},
  {"mouse_has_packet", "tn_mouse_has_packet"}, {"mouse_dx", "tn_mouse_dx"},
  {"mouse_dy", "tn_mouse_dy"}, {"mouse_buttons", "tn_mouse_buttons"},
  {"mouse_pos_x", "tn_mouse_pos_x"}, {"mouse_pos_y", "tn_mouse_pos_y"},
  {"tn_tzimage_decode_file", "tn_tzimage_decode_file"},
  {"tn_tzimage_free", "tn_tzimage_free"}
};

static const char* bare_name(const char* name, int* len){
  for(int i=*len-1;i>=0;i--){
    if(name[i]=='.'){ *len-=i+1; return name+i+1; }
  }
  return name;
}

static int native_symbol(const char* name, int len, char* symbol, int* library){
  if(!name || len<=0) return 0;
  name=bare_name(name,&len);
  if((len>7 && strncmp(name,"tz_cpu_",7)==0) ||
     (len>7 && strncmp(name,"tz_gpu_",7)==0) ||
     (len>8 && strncmp(name,"tz_cuda_",8)==0)){
    int cpu=strncmp(name,"tz_cpu_",7)==0;
    int prefix=cpu ? 0 : 10;
    if(len+prefix>=128) return 0;
    if(prefix) memcpy(symbol,"tn_native_",10);
    memcpy(symbol+prefix,name,(size_t)len); symbol[len+prefix]=0;
    *library=cpu ? TN_NATIVE_CPU : TN_NATIVE_RUNTIME;
    return 1;
  }
  if((len>10 && strncmp(name,"tn_speech_",10)==0) ||
     (len>12 && strncmp(name,"tn_security_",12)==0)){
    if(len>=128) return 0;
    memcpy(symbol,name,(size_t)len); symbol[len]=0;
    *library=TN_NATIVE_RUNTIME; return 1;
  }
  if(len>7 && strncmp(name,"speech_",7)==0){
    if(len>=125) return 0;
    memcpy(symbol,"tn_",3); memcpy(symbol+3,name,(size_t)len); symbol[len+3]=0;
    *library=TN_NATIVE_RUNTIME; return 1;
  }
  if(len>4 && strncmp(name,"tls_",4)==0){
    if(len>=125) return 0;
    memcpy(symbol,"tn_",3); memcpy(symbol+3,name,(size_t)len); symbol[len+3]=0;
    *library=TN_NATIVE_RUNTIME; return 1;
  }
  for(size_t i=0;i<sizeof(runtime_names)/sizeof(runtime_names[0]);i++){
    if((int)strlen(runtime_names[i].name)==len && memcmp(name,runtime_names[i].name,(size_t)len)==0){
      strcpy(symbol,runtime_names[i].symbol); *library=TN_NATIVE_RUNTIME; return 1;
    }
  }
  return 0;
}

static int add_import(TnNativeImports* imports, const char* symbol, int library){
  for(int i=0;i<imports->count;i++){
    if(imports->entries[i].library==library && strcmp(imports->entries[i].symbol,symbol)==0) return i;
  }
  if(imports->count>=TN_NATIVE_IMPORT_MAX){
    fprintf(stderr,"buildexe: too many native runtime imports\n"); return -1;
  }
  int index=imports->count++;
  strcpy(imports->entries[index].symbol,symbol);
  imports->entries[index].library=library;
  imports->entries[index].iat=0;
  return index;
}

static int find_function(IRModule* module, const char* name, int len){
  for(int i=0;i<module->fn_n;i++){
    if(module->fns[i].len==len && memcmp(module->fns[i].name,name,(size_t)len)==0) return i;
  }
  return -1;
}

int tn_native_collect(IRModule* module, TnNativeImports* imports){
  memset(imports,0,sizeof(*imports));
  if(!module) return 0;
  unsigned char* reached=(unsigned char*)calloc((size_t)module->fn_n+1,1);
  int* pending=(int*)malloc(sizeof(int)*((size_t)module->fn_n+1));
  if(!reached || !pending){ free(reached); free(pending); return -1; }
  int count=0, result=0;
  for(int i=0;i<module->fn_n;i++){
    IRFunc* fn=&module->fns[i];
    if(!fn->is_extern && ((fn->len==4 && memcmp(fn->name,"main",4)==0) ||
        (fn->len==9 && memcmp(fn->name,"tezz_init",9)==0) ||
        (fn->len==11 && memcmp(fn->name,"__tezz_init",11)==0))){
      reached[i]=1; pending[count++]=i;
    }
  }
  while(count>0 && result==0){
    IRFunc* fn=&module->fns[pending[--count]];
    for(int j=0;j<fn->n;j++){
      IRIns* in=&fn->ins[j];
      if((in->op!=I_CALL && in->op!=I_ADDRSYM) || !in->name) continue;
      int target=find_function(module,in->name,in->nlen);
      if(target>=0 && !module->fns[target].is_extern){
        if(!reached[target]){ reached[target]=1; pending[count++]=target; }
      } else {
        char symbol[128]; int library=0;
        if(native_symbol(in->name,in->nlen,symbol,&library) && add_import(imports,symbol,library)<0){ result=-1; break; }
      }
    }
  }
  free(reached); free(pending);
  // Keep native allocations and runtime-owned returned buffers in the same CRT.
  if(result==0 && imports->count>0){
    if(add_import(imports,"tn_native_malloc",TN_NATIVE_RUNTIME)<0 ||
       add_import(imports,"tn_native_free",TN_NATIVE_RUNTIME)<0) result=-1;
  }
  return result;
}

int tn_native_find(const TnNativeImports* imports, const char* name, int len){
  char symbol[128]; int library=0;
  if(!imports || !native_symbol(name,len,symbol,&library)) return -1;
  for(int i=0;i<imports->count;i++){
    if(imports->entries[i].library==library && strcmp(imports->entries[i].symbol,symbol)==0) return i;
  }
  return -1;
}

const char* tn_native_library_name(int library, int windows){
  static const char* win_names[]={"tnrt.dll","tzcpu.dll","tzgpu.dll"};
  static const char* unix_names[]={"libtnrt-native.so","libtzcpu-native.so","libtzgpu-native.so"};
  return (windows ? win_names : unix_names)[library];
}

static int file_exists(const char* path){ FILE* f=fopen(path,"rb"); if(!f) return 0; fclose(f); return 1; }

int tn_native_library_path(const char* name, char* path, size_t cap){
  char executable[4096]={0};
#ifdef _WIN32
  if(!GetModuleFileNameA(NULL,executable,sizeof(executable))) executable[0]=0;
#else
  ssize_t length=readlink("/proc/self/exe",executable,sizeof(executable)-1);
  if(length>0) executable[length]=0;
#endif
  char* slash=strrchr(executable,'/'); char* backslash=strrchr(executable,'\\');
  if(backslash && (!slash || backslash>slash)) slash=backslash;
  if(slash){
    *slash=0;
    snprintf(path,cap,"%s/%s",executable,name);
    if(file_exists(path)) return 0;
  }
  const char* sdk=getenv("TEZZ_SDK_ROOT");
  if(sdk && *sdk){
    snprintf(path,cap,"%s/bin/%s",sdk,name); if(file_exists(path)) return 0;
    snprintf(path,cap,"%s/lib/%s",sdk,name); if(file_exists(path)) return 0;
  }
  return -1;
}

static int real_gpu_library(const char* path){
#if !defined(_WIN32) && defined(__x86_64__)
  typedef int64_t (__attribute__((ms_abi)) *BackendKind)(void);
#else
  typedef int64_t (*BackendKind)(void);
#endif
#ifdef _WIN32
  HMODULE module=LoadLibraryExA(path,NULL,LOAD_WITH_ALTERED_SEARCH_PATH);
  if(!module) return 0;
  BackendKind kind=(BackendKind)(void*)GetProcAddress(module,"tz_cuda_backend_kind");
  int real=kind && kind()==1;
  FreeLibrary(module);
#else
  void* module=dlopen(path,RTLD_NOW|RTLD_LOCAL);
  if(!module) return 0;
  BackendKind kind=(BackendKind)dlsym(module,"tz_cuda_backend_kind");
  int real=kind && kind()==1;
  dlclose(module);
#endif
  return real;
}

int tn_native_deploy(const TnNativeImports* imports, const char* output, int windows, int machine){
  int gpu_bridge=0;
  for(int i=0;i<imports->count;i++){
    if(strncmp(imports->entries[i].symbol,"tn_native_tz_gpu_",17)==0 ||
       strncmp(imports->entries[i].symbol,"tn_native_tz_cuda_",18)==0) gpu_bridge=1;
  }
  for(int library=0;library<TN_NATIVE_LIBRARY_COUNT;library++){
    int needed=0;
    for(int i=0;i<imports->count;i++) if(imports->entries[i].library==library) needed=1;
    int optional=library==TN_NATIVE_GPU && gpu_bridge && !needed;
    if(!needed && !optional) continue;
    const char* name=tn_native_library_name(library,windows);
    char source[4096], destination[4096];
    if(tn_native_library_path(name,source,sizeof(source))<0){
      if(optional) continue;
      fprintf(stderr,"buildexe: required native library '%s' is missing; build the SDK native runtime libraries and place them beside tezzc or in TEZZ_SDK_ROOT/bin\n",name);
      return -1;
    }
    if(optional && !real_gpu_library(source)) continue;
    snprintf(destination,sizeof(destination),"%s",output);
    char* slash=strrchr(destination,'/'); char* backslash=strrchr(destination,'\\');
    if(backslash && (!slash || backslash>slash)) slash=backslash;
    size_t prefix=slash ? (size_t)(slash-destination)+1 : 0;
    if(prefix+strlen(name)>=sizeof(destination)) return -1;
    strcpy(destination+prefix,name);
#ifdef _WIN32
    if(windows){
      FILE* input=fopen(source,"rb"); unsigned char header[64]; uint32_t pe=0; uint16_t arch=0;
      int valid=input && fread(header,1,sizeof(header),input)==sizeof(header) && header[0]=='M' && header[1]=='Z';
      if(valid){ memcpy(&pe,header+60,4); valid=fseek(input,(long)pe+4,SEEK_SET)==0 && fread(&arch,2,1,input)==1 && arch==machine; }
      if(input) fclose(input);
      if(!valid){ fprintf(stderr,"buildexe: native library '%s' does not match the requested target architecture\n",source); return -1; }
      HMODULE module=LoadLibraryExA(source,NULL,DONT_RESOLVE_DLL_REFERENCES);
      if(!module){ fprintf(stderr,"buildexe: cannot inspect native library '%s'\n",source); return -1; }
      for(int i=0;i<imports->count;i++){
        if(imports->entries[i].library==library && !GetProcAddress(module,imports->entries[i].symbol)){
          fprintf(stderr,"buildexe: native library '%s' does not export '%s'\n",source,imports->entries[i].symbol);
          FreeLibrary(module); return -1;
        }
      }
      FreeLibrary(module);
      char full_source[4096], full_destination[4096];
      if(!GetFullPathNameA(source,sizeof(full_source),full_source,NULL) ||
         !GetFullPathNameA(destination,sizeof(full_destination),full_destination,NULL)) return -1;
      if(_stricmp(full_source,full_destination)!=0 && !CopyFileA(source,destination,FALSE)){
        fprintf(stderr,"buildexe: cannot copy native library '%s' beside output\n",name); return -1;
      }
      continue;
    }
#endif
    if(!windows){
      FILE* input=fopen(source,"rb"); unsigned char header[64]; uint16_t arch=0;
      int valid=input && fread(header,1,sizeof(header),input)==sizeof(header) &&
        header[0]==0x7f && header[1]=='E' && header[2]=='L' && header[3]=='F' && header[4]==2 && header[5]==1;
      if(valid){ memcpy(&arch,header+18,2); valid=arch==machine; }
      if(input) fclose(input);
      if(!valid){ fprintf(stderr,"buildexe: native library '%s' does not match the requested ELF target\n",source); return -1; }
    }
#ifndef _WIN32
    struct stat source_info, destination_info;
    if(stat(source,&source_info)==0 && stat(destination,&destination_info)==0 &&
       source_info.st_dev==destination_info.st_dev && source_info.st_ino==destination_info.st_ino) continue;
#endif
    if(strcmp(source,destination)==0) continue;
    FILE* input=fopen(source,"rb"); FILE* target=fopen(destination,"wb");
    if(!input || !target){ if(input) fclose(input); if(target) fclose(target); return -1; }
    unsigned char buffer[16384]; size_t bytes; int failed=0;
    while((bytes=fread(buffer,1,sizeof(buffer),input))>0){ if(fwrite(buffer,1,bytes,target)!=bytes){ failed=1; break; } }
    if(ferror(input)) failed=1;
    fclose(input); if(fclose(target)!=0) failed=1;
    if(failed) return -1;
  }
  return 0;
}
