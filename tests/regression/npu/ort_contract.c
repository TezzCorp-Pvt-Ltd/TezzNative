/* Typed vendor API test double: validates adapters, never claims NPU execution. */
#include "ort_bridge.c"
#include <assert.h>
#include <stdio.h>
struct OrtEnv { int unused; };
struct OrtStatus { int unused; };
struct OrtSessionOptions { int no_fallback, selected; };
struct OrtSession { int unused; };
struct OrtMemoryInfo { int unused; };
struct OrtTensorTypeAndShapeInfo { int unused; };
struct OrtTypeInfo { struct OrtTensorTypeAndShapeInfo tensor; };
struct OrtValue { float* data; };
struct OrtHardwareDevice { OrtHardwareDeviceType type; };
struct OrtEpDevice { const char* name; struct OrtHardwareDevice hardware; };
static int live, calls, fail_call, available = 1, wrong_assignment, bad_shape, bad_type, multi_input;
static int run_failure, bad_output, telemetry_disabled;
#ifdef TN_NPU_ORT_AMD
#define PROVIDER "VitisAIExecutionProvider"
#else
#define PROVIDER "QNNExecutionProvider"
#endif
static struct OrtEpDevice cpu = {PROVIDER,{OrtHardwareDeviceType_CPU}};
static struct OrtEpDevice gpu = {PROVIDER,{OrtHardwareDeviceType_GPU}};
static struct OrtEpDevice other = {"OtherExecutionProvider",{OrtHardwareDeviceType_NPU}};
static struct OrtEpDevice npu = {PROVIDER,{OrtHardwareDeviceType_NPU}};
static const OrtEpDevice* devices[] = {&cpu,&gpu,&other,&npu};
static void* allocate(size_t n){void* p=calloc(1,n);assert(p);++live;return p;}
static void release(void* p){if(p){--live;free(p);}}
static OrtStatus* fail(void){return ++calls==fail_call?(OrtStatus*)allocate(sizeof(OrtStatus)):NULL;}
#define CHECKED() do { OrtStatus* error=fail();if(error)return error; } while(0)
static OrtStatus* ORT_API_CALL create_env(OrtLoggingLevel level,const char* name,OrtEnv** env){
  assert(level==ORT_LOGGING_LEVEL_ERROR);assert(name);CHECKED();*env=allocate(sizeof(**env));return NULL;
}
static void ORT_API_CALL release_env(OrtEnv* p){release(p);}
static void ORT_API_CALL release_status(OrtStatus* p){release(p);}
static OrtStatus* ORT_API_CALL telemetry(const OrtEnv* env){assert(env);CHECKED();telemetry_disabled=1;return NULL;}
static void ORT_API_CALL allocator_free(OrtAllocator* a,void* p){(void)a;release(p);}
static OrtAllocator allocator={.version=ORT_API_VERSION,.Free=allocator_free};
static OrtStatus* ORT_API_CALL get_allocator(OrtAllocator** out){CHECKED();*out=&allocator;return NULL;}
static OrtStatus* ORT_API_CALL register_ep(OrtEnv* env,const char* name,const ORTCHAR_T* path){
  assert(env&&name&&path);CHECKED();return NULL;
}
static OrtStatus* ORT_API_CALL get_devices(const OrtEnv* env,const OrtEpDevice* const** out,size_t* count){
  assert(env&&telemetry_disabled);CHECKED();*out=devices;*count=available?4:3;return NULL;
}
static const char* ORT_API_CALL ep_name(const OrtEpDevice* d){return d->name;}
static const OrtHardwareDevice* ORT_API_CALL ep_device(const OrtEpDevice* d){return &d->hardware;}
static OrtHardwareDeviceType ORT_API_CALL device_type(const OrtHardwareDevice* d){return d->type;}
static OrtStatus* ORT_API_CALL create_options(OrtSessionOptions** out){CHECKED();*out=allocate(sizeof(**out));return NULL;}
static void ORT_API_CALL release_options(OrtSessionOptions* p){release(p);}
static OrtStatus* ORT_API_CALL config(OrtSessionOptions* options,const char* key,const char* value){
  assert(!strcmp(key,"session.disable_cpu_ep_fallback")&&!strcmp(value,"1"));CHECKED();options->no_fallback=1;return NULL;
}
static OrtStatus* ORT_API_CALL append(OrtSessionOptions* options,OrtEnv* env,const OrtEpDevice* const* selected,
                                     size_t count,const char* const* keys,const char* const* values,size_t n){
  assert(env&&options->no_fallback&&count==1&&selected[0]==&npu);
#ifdef TN_NPU_ORT_QNN
  assert(n==2&&!strcmp(keys[0],"backend_type")&&!strcmp(values[0],"htp"));
  assert(!strcmp(keys[1],"offload_graph_io_quantization")&&!strcmp(values[1],"0"));
#else
  assert(!n&&!keys&&!values);
#endif
  CHECKED();options->selected=1;return NULL;
}
static OrtStatus* ORT_API_CALL create_session(const OrtEnv* env,const ORTCHAR_T* path,const OrtSessionOptions* options,OrtSession** out){
  assert(env&&path&&options->selected&&options->no_fallback);CHECKED();*out=allocate(sizeof(**out));return NULL;
}
static void ORT_API_CALL release_session(OrtSession* p){release(p);}
static OrtStatus* ORT_API_CALL input_count(const OrtSession* s,size_t* n){assert(s);CHECKED();*n=multi_input?2:1;return NULL;}
static OrtStatus* ORT_API_CALL output_count(const OrtSession* s,size_t* n){assert(s);CHECKED();*n=1;return NULL;}
static OrtStatus* ORT_API_CALL assigned(const OrtSession* s,const OrtEpDevice** d,size_t n){
  assert(s&&n==1);CHECKED();*d=wrong_assignment?&cpu:&npu;return NULL;
}
static OrtStatus* ORT_API_CALL type_info(const OrtSession* s,size_t i,OrtTypeInfo** out){
  assert(s&&!i);CHECKED();*out=allocate(sizeof(**out));return NULL;
}
static void ORT_API_CALL release_info(OrtTypeInfo* p){release(p);}
static OrtStatus* ORT_API_CALL cast_info(const OrtTypeInfo* info,const OrtTensorTypeAndShapeInfo** out){CHECKED();*out=&info->tensor;return NULL;}
static OrtStatus* ORT_API_CALL element_type(const OrtTensorTypeAndShapeInfo* info,ONNXTensorElementDataType* out){
  assert(info);CHECKED();*out=bad_type?ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16:ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;return NULL;
}
static OrtStatus* ORT_API_CALL rank(const OrtTensorTypeAndShapeInfo* info,size_t* n){assert(info);CHECKED();*n=bad_shape==3?9:2;return NULL;}
static OrtStatus* ORT_API_CALL dims(const OrtTensorTypeAndShapeInfo* info,int64_t* out,size_t n){
  assert(info&&n==2);CHECKED();out[0]=bad_shape==1?-1:bad_shape==2?INT64_MAX:1;out[1]=4;return NULL;
}
static OrtStatus* ORT_API_CALL tensor_name(const OrtSession* s,size_t i,OrtAllocator* a,char** out){
  assert(s&&!i&&a);CHECKED();*out=allocate(7);strcpy(*out,"tensor");return NULL;
}
static OrtStatus* ORT_API_CALL memory_info(enum OrtAllocatorType type,enum OrtMemType mem,OrtMemoryInfo** out){
  assert(type==OrtArenaAllocator&&mem==OrtMemTypeDefault);CHECKED();*out=allocate(sizeof(**out));return NULL;
}
static void ORT_API_CALL release_memory(OrtMemoryInfo* p){release(p);}
static OrtStatus* ORT_API_CALL tensor(const OrtMemoryInfo* info,void* data,size_t bytes,const int64_t* shape,
                                     size_t n,ONNXTensorElementDataType type,OrtValue** out){
  assert(info&&data&&bytes==16&&n==2&&shape[0]==1&&shape[1]==4&&type==ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
  CHECKED();*out=allocate(sizeof(**out));(*out)->data=data;return NULL;
}
static void ORT_API_CALL release_value(OrtValue* p){release(p);}
static OrtStatus* ORT_API_CALL run(OrtSession* s,const OrtRunOptions* options,const char* const* names,
                                  const OrtValue* const* input,size_t n,const char* const* outputs,size_t m,OrtValue** out){
  assert(s&&!options&&names&&outputs&&n==1&&m==1&&*out);CHECKED();
  if(run_failure)return allocate(sizeof(OrtStatus));
  for(int i=0;i<4;++i)(*out)->data[i]=fmaxf(0,input[0]->data[i]);
  if(bad_output)(*out)->data[3]=INFINITY;
  return NULL;
}
static const OrtApi api={
  .CreateEnv=create_env,.ReleaseEnv=release_env,.ReleaseStatus=release_status,
  .DisableTelemetryEvents=telemetry,.GetAllocatorWithDefaultOptions=get_allocator,
  .RegisterExecutionProviderLibrary=register_ep,.GetEpDevices=get_devices,
  .EpDevice_EpName=ep_name,.EpDevice_Device=ep_device,.HardwareDevice_Type=device_type,
  .CreateSessionOptions=create_options,.ReleaseSessionOptions=release_options,.AddSessionConfigEntry=config,
  .SessionOptionsAppendExecutionProvider_V2=append,.CreateSession=create_session,.ReleaseSession=release_session,
  .SessionGetInputCount=input_count,.SessionGetOutputCount=output_count,.SessionGetEpDeviceForInputs=assigned,
  .SessionGetInputTypeInfo=type_info,.SessionGetOutputTypeInfo=type_info,.ReleaseTypeInfo=release_info,
  .CastTypeInfoToTensorInfo=cast_info,.GetTensorElementType=element_type,.GetDimensionsCount=rank,.GetDimensions=dims,
  .SessionGetInputName=tensor_name,.SessionGetOutputName=tensor_name,.CreateCpuMemoryInfo=memory_info,
  .ReleaseMemoryInfo=release_memory,.CreateTensorWithDataAsOrtValue=tensor,.ReleaseValue=release_value,.Run=run
};
static const OrtApi* ORT_API_CALL get_api(uint32_t version){assert(version==23);return &api;}
static const OrtApiBase base={.GetApi=get_api};
const OrtApiBase* ORT_API_CALL OrtGetApiBase(void){return &base;}
static void rejected(long long h,double* input,long long in_n,long long out_n){
  double output[4]={41,42,43,44};assert(tn_npu_model_run_f64_1d(h,input,in_n,output,out_n)==-1);
  for(int i=0;i<4;++i)assert(output[i]==41+i);
}
int main(void){
#ifdef _WIN32
  _putenv("TN_NPU_EP_LIBRARY=");
#else
  unsetenv("TN_NPU_EP_LIBRARY");
#endif
  available=0;assert(!tn_npu_has_backend());assert(tn_npu_init()==-1);assert(tn_npu_model_load((unsigned char*)"model")==-1);
  assert(!tn_npu_shutdown());assert(!live);available=1;
  for(int i=1;i<=4;++i){calls=0;fail_call=i;assert(!tn_npu_has_backend());assert(!tn_npu_shutdown());assert(!live);}
  fail_call=0;assert(tn_npu_device_count()==1);assert(!tn_npu_set_device(0));assert(tn_npu_set_device(1)==-1);
  wrong_assignment=1;assert(tn_npu_model_load((unsigned char*)"model")==-1);assert(live==1);wrong_assignment=0;
  bad_type=1;assert(tn_npu_model_load((unsigned char*)"model")==-1);assert(live==1);bad_type=0;
  multi_input=1;assert(tn_npu_model_load((unsigned char*)"model")==-1);assert(live==1);multi_input=0;
  for(bad_shape=1;bad_shape<=3;++bad_shape){assert(tn_npu_model_load((unsigned char*)"model")==-1);assert(live==1);}bad_shape=0;
  calls=0;long long h=tn_npu_model_load((unsigned char*)"model");assert(h>0);int load_calls=calls;
  assert(tn_npu_set_device(0)==-1);assert(!tn_npu_model_free(h));assert(tn_npu_model_free(h)==-1);
  for(int i=1;i<=load_calls;++i){calls=0;fail_call=i;assert(tn_npu_model_load((unsigned char*)"model")==-1);assert(live==1);}
  fail_call=0;h=tn_npu_model_load((unsigned char*)"model");assert(h>0);
  double input[4]={-2,0,1.25,7},output[4]={0};
  calls=0;assert(!tn_npu_model_run_f64_1d(h,input,4,output,4));int run_calls=calls;
  assert(output[0]==0&&output[1]==0&&output[2]==1.25&&output[3]==7);
  for(int i=1;i<=run_calls;++i){calls=0;fail_call=i;rejected(h,input,4,4);assert(live==4);}fail_call=0;
  rejected(h,input,3,4);rejected(h,input,4,3);rejected(h,input,LLONG_MAX,4);rejected(h,NULL,4,4);
  input[2]=NAN;rejected(h,input,4,4);input[2]=INFINITY;rejected(h,input,4,4);
  input[2]=(double)FLT_MAX*2;rejected(h,input,4,4);input[2]=1.25;
  run_failure=1;rejected(h,input,4,4);run_failure=0;
  bad_output=1;rejected(h,input,4,4);bad_output=0;
  assert(!tn_npu_model_run_f64_1d(h,input,4,input,4));assert(input[0]==0&&input[3]==7);
  assert(!tn_npu_shutdown());assert(!live);rejected(h,input,4,4);
  long long next=tn_npu_model_load((unsigned char*)"model");assert(next>h);assert(!tn_npu_shutdown());assert(!live);
  printf("PASS %s contract: selection, no fallback, lifecycle, %d load and %d run failure points (test double; no device)\n",PROVIDER,load_calls,run_calls);
  return 0;
}
