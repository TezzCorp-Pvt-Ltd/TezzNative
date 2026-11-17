/* Provider-contract test double. This is NOT an NPU device execution test.
 * Compiled against the vendor's actual C headers to check API signatures.
 */
#define OPENVINO_STATIC_LIBRARY
#include "openvino_bridge.c"
#include <assert.h>
#include <stdio.h>

struct ov_core { int unused; };
struct ov_model { int unused; };
struct ov_compiled_model { int unused; };
struct ov_output_const_port { int output; };
struct ov_infer_request { float input[4], output[4], replacement[4]; int replaced; };
struct ov_tensor { float* data; };
static int live, calls, fail_call, has_npu = 1, bad_type, bad_shape, multi_input;
static int infer_fail, nonfinite_output, replace_output;
static const char* execution = "NPU.0";
static int fail(void){ return ++calls == fail_call; }
static void* allocate(size_t n){ void* p = calloc(1, n); assert(p); ++live; return p; }
static void release(void* p){ if(p){ --live; free(p); } }
static char* duplicate(const char* s){ char* p = allocate(strlen(s)+1); strcpy(p,s); return p; }
#define CHECKED() do { if(fail()) return GENERAL_ERROR; } while(0)
ov_status_e ov_core_create(ov_core_t** p){ CHECKED(); *p=allocate(sizeof(**p)); return OK; }
void ov_core_free(ov_core_t* p){ release(p); }
ov_status_e ov_core_get_available_devices(const ov_core_t* c,ov_available_devices_t* d){
  (void)c; CHECKED(); d->size=has_npu?4:2; d->devices=allocate(d->size*sizeof(char*));
  d->devices[0]=duplicate("CPU"); d->devices[1]=duplicate("GPU.0");
  if(has_npu){ d->devices[2]=duplicate("NPU.0"); d->devices[3]=duplicate("NPU.1"); }
  return OK;
}
void ov_available_devices_free(ov_available_devices_t* d){
  for(size_t i=0;i<d->size;++i) release(d->devices[i]); release(d->devices);
}
ov_status_e ov_core_read_model(const ov_core_t* c,const char* p,const char* b,ov_model_t** m){
  (void)c;(void)p;(void)b;CHECKED();*m=allocate(sizeof(**m));return OK;
}
void ov_model_free(ov_model_t* m){release(m);}
ov_status_e ov_core_compile_model(const ov_core_t* c,const ov_model_t* m,const char* device,
                                 const size_t properties,ov_compiled_model_t** out,...){
  (void)c;(void)m;assert(!strcmp(device,"NPU.0")||!strcmp(device,"NPU.1"));assert(!properties);
  CHECKED();*out=allocate(sizeof(**out));return OK;
}
void ov_compiled_model_free(ov_compiled_model_t* m){release(m);}
ov_status_e ov_compiled_model_get_property(const ov_compiled_model_t* m,const char* key,char** value){
  (void)m;assert(!strcmp(key,"EXECUTION_DEVICES"));CHECKED();*value=duplicate(execution);return OK;
}
void ov_free(const char* p){release((void*)p);}
ov_status_e ov_compiled_model_inputs_size(const ov_compiled_model_t* m,size_t* n){
  (void)m;CHECKED();*n=multi_input?2:1;return OK;
}
ov_status_e ov_compiled_model_outputs_size(const ov_compiled_model_t* m,size_t* n){
  (void)m;CHECKED();*n=1;return OK;
}
ov_status_e ov_compiled_model_input(const ov_compiled_model_t* m,ov_output_const_port_t** p){
  (void)m;CHECKED();*p=allocate(sizeof(**p));return OK;
}
ov_status_e ov_compiled_model_output(const ov_compiled_model_t* m,ov_output_const_port_t** p){
  (void)m;CHECKED();*p=allocate(sizeof(**p));(*p)->output=1;return OK;
}
void ov_output_const_port_free(ov_output_const_port_t* p){release(p);}
ov_status_e ov_port_get_element_type(const ov_output_const_port_t* p,ov_element_type_e* type){
  (void)p;CHECKED();*type=bad_type?F16:F32;return OK;
}
ov_status_e ov_const_port_get_shape(const ov_output_const_port_t* p,ov_shape_t* s){
  (void)p;CHECKED();s->rank=2;s->dims=allocate(2*sizeof(int64_t));s->dims[0]=1;s->dims[1]=4;
  if(bad_shape==1)s->dims[0]=-1;
  if(bad_shape==2)s->dims[0]=INT64_MAX;
  if(bad_shape==3)s->dims[0]=0;
  if(bad_shape==4)s->rank=0;
  if(bad_shape==5)s->rank=9;
  return OK;
}
ov_status_e ov_shape_free(ov_shape_t* s){release(s->dims);s->dims=NULL;s->rank=0;return OK;}
ov_status_e ov_compiled_model_create_infer_request(const ov_compiled_model_t* m,ov_infer_request_t** r){
  (void)m;CHECKED();*r=allocate(sizeof(**r));return OK;
}
void ov_infer_request_free(ov_infer_request_t* r){release(r);}
ov_status_e ov_infer_request_get_input_tensor(const ov_infer_request_t* r,ov_tensor_t** t){
  CHECKED();*t=allocate(sizeof(**t));(*t)->data=(float*)r->input;return OK;
}
ov_status_e ov_infer_request_get_output_tensor(const ov_infer_request_t* r,ov_tensor_t** t){
  CHECKED();*t=allocate(sizeof(**t));(*t)->data=(float*)(r->replaced?r->replacement:r->output);return OK;
}
void ov_tensor_free(ov_tensor_t* t){release(t);}
ov_status_e ov_tensor_get_element_type(const ov_tensor_t* t,ov_element_type_e* type){
  (void)t;CHECKED();*type=F32;return OK;
}
ov_status_e ov_tensor_get_size(const ov_tensor_t* t,size_t* n){(void)t;CHECKED();*n=4;return OK;}
ov_status_e ov_tensor_get_byte_size(const ov_tensor_t* t,size_t* n){(void)t;CHECKED();*n=16;return OK;}
ov_status_e ov_tensor_data(const ov_tensor_t* t,void** data){CHECKED();*data=t->data;return OK;}
ov_status_e ov_infer_request_infer(ov_infer_request_t* r){
  CHECKED();if(infer_fail)return GENERAL_ERROR;
  r->replaced=replace_output;
  float* output=r->replaced?r->replacement:r->output;
  for(int i=0;i<4;++i)output[i]=fmaxf(0,r->input[i]);
  if(nonfinite_output)output[3]=NAN;
  if(replace_output)r->output[0]=NAN;
  return OK;
}
static void rejected_run(long long h,double* x,long long in_n,long long out_n){
  double output[4]={41,42,43,44};
  assert(tn_npu_model_run_f64_1d(h,x,in_n,output,out_n)==-1);
  for(int i=0;i<4;++i)assert(output[i]==41+i);
}
int main(void){
  const char* rejected[]={"CPU","GPU.0","AUTO:NPU","HETERO:NPU,CPU","NPU,CPU","NPU CPU","NPUx","NPU.","NPU.-1",""};
  for(size_t i=0;i<sizeof(rejected)/sizeof(*rejected);++i)assert(!tn_ov_execution_device(rejected[i]));
  assert(tn_ov_execution_device("[ NPU.0 ]"));assert(tn_ov_execution_device("NPU"));
  has_npu=0;assert(!tn_npu_has_backend());assert(!tn_npu_device_count());assert(tn_npu_init()==-1);
  assert(tn_npu_model_load((unsigned char*)"model")==-1);assert(!tn_npu_shutdown());assert(!live);
  has_npu=1;assert(tn_npu_device_count()==2);assert(!tn_npu_set_device(1));assert(tn_npu_set_device(2)==-1);
  assert(tn_npu_set_device(-1)==-1);assert(!tn_npu_init());
  for(size_t i=0;i<sizeof(rejected)/sizeof(*rejected);++i){
    execution=rejected[i];assert(tn_npu_model_load((unsigned char*)"model")==-1);assert(live==1);
  }
  execution="NPU.0";
  bad_type=1;assert(tn_npu_model_load((unsigned char*)"model")==-1);assert(live==1);bad_type=0;
  multi_input=1;assert(tn_npu_model_load((unsigned char*)"model")==-1);assert(live==1);multi_input=0;
  for(bad_shape=1;bad_shape<=5;++bad_shape){assert(tn_npu_model_load((unsigned char*)"model")==-1);assert(live==1);}
  bad_shape=0;
  calls=0;long long h=tn_npu_model_load((unsigned char*)"model");assert(h>0);int load_calls=calls;
  assert(tn_npu_set_device(0)==-1);assert(!tn_npu_model_free(h));assert(tn_npu_model_free(h)==-1);
  for(int i=1;i<=load_calls;++i){
    calls=0;fail_call=i;assert(tn_npu_model_load((unsigned char*)"model")==-1);assert(live==1);
  }
  fail_call=0;h=tn_npu_model_load((unsigned char*)"model");assert(h>0);
  double input[4]={-1,0,1.25,3.5},output[4]={0};
  calls=0;assert(!tn_npu_model_run_f64_1d(h,input,4,output,4));int run_calls=calls;
  assert(output[0]==0&&output[1]==0&&output[2]==1.25&&output[3]==3.5);
  for(int i=1;i<=run_calls;++i){calls=0;fail_call=i;rejected_run(h,input,4,4);assert(live==3);}
  fail_call=0;
  rejected_run(h,input,3,4);rejected_run(h,input,4,3);rejected_run(h,input,LLONG_MAX,4);
  rejected_run(-1,input,4,4);rejected_run(h,NULL,4,4);
  input[2]=INFINITY;rejected_run(h,input,4,4);input[2]=NAN;rejected_run(h,input,4,4);
  input[2]=(double)FLT_MAX*2;rejected_run(h,input,4,4);input[2]=1.25;
  infer_fail=1;rejected_run(h,input,4,4);infer_fail=0;
  nonfinite_output=1;rejected_run(h,input,4,4);nonfinite_output=0;
  replace_output=1;assert(!tn_npu_model_run_f64_1d(h,input,4,output,4));assert(output[2]==1.25);
  assert(!tn_npu_model_run_f64_1d(h,input,4,input,4));assert(input[0]==0&&input[3]==3.5);
  assert(!tn_npu_alloc(16));assert(tn_npu_supports_dxil()==0);
  assert(!tn_npu_shutdown());assert(!live);rejected_run(h,input,4,4);
  long long next=tn_npu_model_load((unsigned char*)"model");assert(next>h);
  assert(!tn_npu_shutdown());assert(!live);
  printf("PASS OpenVINO provider contract: selection, lifecycle, %d load and %d run failure points, buffers (test double; no device)\n",load_calls,run_calls);
  return 0;
}
