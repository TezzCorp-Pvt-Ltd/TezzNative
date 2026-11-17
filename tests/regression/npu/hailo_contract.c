/* Official-header provider test double. No Hailo device is emulated or verified. */
#include "hailo_bridge.c"
#include <assert.h>
#include <stdio.h>

struct _hailo_hef { int unused; };
struct _hailo_configured_network_group { int unused; };
struct _hailo_vdevice { hailo_device_id_t id; struct _hailo_configured_network_group group; };
struct _hailo_activated_network_group { int unused; };
static int live, calls, fail_call, has_device = 1, scan_bad_name, wrong_physical;
static int multiple_groups, multiple_inputs, multiple_outputs, bad_info, bad_bytes;
static int infer_fail, nonfinite_output, active_count;
static void* allocate(size_t n){ void* p = calloc(1,n); assert(p); ++live; return p; }
static void release(void* p){ if(p){ --live; free(p); } }
static int fail(void){ return ++calls == fail_call; }
#define CHECKED() do { if(fail()) return HAILO_INTERNAL_FAILURE; } while(0)

hailo_status hailo_scan_devices(hailo_scan_devices_params_t* p,hailo_device_id_t* ids,size_t* n){
  assert(!p && *n==64); CHECKED(); *n=has_device?2:0;
  if(has_device){
    strcpy(ids[0].id,"0000:01:00.0"); strcpy(ids[1].id,"0000:02:00.0");
    if(scan_bad_name)memset(ids[0].id,'a',sizeof(ids[0].id));
  }
  return HAILO_SUCCESS;
}
hailo_status hailo_create_hef_file(hailo_hef* h,const char* path){
  assert(path && *path); CHECKED(); *h=allocate(sizeof(**h)); return HAILO_SUCCESS;
}
hailo_status hailo_release_hef(hailo_hef h){ release(h); return HAILO_SUCCESS; }
hailo_status hailo_init_vdevice_params(hailo_vdevice_params_t* p){
  CHECKED(); memset(p,0,sizeof(*p)); return HAILO_SUCCESS;
}
hailo_status hailo_create_vdevice(hailo_vdevice_params_t* p,hailo_vdevice* d){
  assert(p->device_count==1 && p->device_ids);
  assert(p->scheduling_algorithm==HAILO_SCHEDULING_ALGORITHM_NONE && !p->multi_process_service);
  assert(!strcmp(p->group_id,HAILO_UNIQUE_VDEVICE_GROUP_ID));
  assert(!strcmp(p->device_ids[0].id,"0000:01:00.0") || !strcmp(p->device_ids[0].id,"0000:02:00.0"));
  CHECKED(); *d=allocate(sizeof(**d)); (*d)->id=p->device_ids[0]; return HAILO_SUCCESS;
}
hailo_status hailo_vdevice_get_physical_devices_ids(hailo_vdevice d,hailo_device_id_t* ids,size_t* n){
  assert(*n==1); CHECKED(); *n=1;
  /* Valid strings need not have equal unused padding bytes. */
  memset(ids,0x7f,sizeof(*ids)); strcpy(ids->id,wrong_physical?"0000:03:00.0":d->id.id);
  return HAILO_SUCCESS;
}
hailo_status hailo_release_vdevice(hailo_vdevice d){ assert(!active_count); release(d); return HAILO_SUCCESS; }
hailo_status hailo_init_configure_params_by_vdevice(hailo_hef h,hailo_vdevice d,hailo_configure_params_t* p){
  assert(h&&d); CHECKED(); memset(p,0,sizeof(*p)); p->network_group_params_count=multiple_groups?2:1;
  return HAILO_SUCCESS;
}
hailo_status hailo_configure_vdevice(hailo_vdevice d,hailo_hef h,hailo_configure_params_t* p,
                                    hailo_configured_network_group* g,size_t* n){
  assert(h && *n==1 && p->network_group_params[0].batch_size==1); CHECKED();
  *g=&d->group; *n=1; return HAILO_SUCCESS;
}
hailo_status hailo_make_input_vstream_params(hailo_configured_network_group g,bool unused,
    hailo_format_type_t type,hailo_input_vstream_params_by_name_t* p,size_t* n){
  assert(g && !unused && type==HAILO_FORMAT_TYPE_FLOAT32 && *n==1); CHECKED();
  *n=multiple_inputs?2:1; strcpy(p->name,"input"); return HAILO_SUCCESS;
}
hailo_status hailo_make_output_vstream_params(hailo_configured_network_group g,bool unused,
    hailo_format_type_t type,hailo_output_vstream_params_by_name_t* p,size_t* n){
  assert(g && !unused && type==HAILO_FORMAT_TYPE_FLOAT32 && *n==1); CHECKED();
  *n=multiple_outputs?2:1; strcpy(p->name,"output"); return HAILO_SUCCESS;
}
hailo_status hailo_hef_get_all_vstream_infos(hailo_hef h,const char* name,hailo_vstream_info_t* info,size_t* n){
  assert(h && !name && *n==2); CHECKED(); *n=2;
  for(int i=0;i<2;++i){
    strcpy(info[i].name,i?"output":"input");
    info[i].direction=i?HAILO_D2H_STREAM:HAILO_H2D_STREAM;
    info[i].format.order=HAILO_FORMAT_ORDER_NHWC;
    info[i].format.type=HAILO_FORMAT_TYPE_UINT8;
    info[i].shape.height=1; info[i].shape.width=1; info[i].shape.features=4;
  }
  if(bad_info==1)info[1].direction=HAILO_H2D_STREAM;
  if(bad_info==2)info[1].format.order=HAILO_FORMAT_ORDER_HAILO_NMS;
  if(bad_info==3)info[1].shape.height=0;
  if(bad_info==4)info[1].shape.height=info[1].shape.width=info[1].shape.features=UINT32_MAX;
  if(bad_info==5)strcpy(info[1].name,"wrong");
  if(bad_info==6)memset(info[1].name,'x',sizeof(info[1].name));
  return HAILO_SUCCESS;
}
hailo_status hailo_get_vstream_frame_size(hailo_vstream_info_t* i,hailo_format_t* format,size_t* n){
  assert(i && format->type==HAILO_FORMAT_TYPE_FLOAT32 && format->order==HAILO_FORMAT_ORDER_NHWC);
  assert(format->flags==HAILO_FORMAT_FLAGS_NONE); CHECKED(); *n=bad_bytes?15:16; return HAILO_SUCCESS;
}
hailo_status hailo_activate_network_group(hailo_configured_network_group g,
    hailo_activate_network_group_params_t* p,hailo_activated_network_group* a){
  assert(g && !p && !active_count); CHECKED(); *a=allocate(sizeof(**a)); ++active_count; return HAILO_SUCCESS;
}
hailo_status hailo_deactivate_network_group(hailo_activated_network_group a){
  assert(active_count==1); --active_count; release(a);
  return fail()?HAILO_INTERNAL_FAILURE:HAILO_SUCCESS;
}
hailo_status hailo_infer(hailo_configured_network_group g,
    hailo_input_vstream_params_by_name_t* ip,hailo_stream_raw_buffer_by_name_t* in,size_t inputs,
    hailo_output_vstream_params_by_name_t* op,hailo_stream_raw_buffer_by_name_t* out,size_t outputs,size_t frames){
  assert(g && active_count==1 && inputs==1 && outputs==1 && frames==1);
  assert(!strcmp(ip->name,in->name) && !strcmp(op->name,out->name));
  assert(ip->params.user_buffer_format.type==HAILO_FORMAT_TYPE_FLOAT32);
  assert(op->params.user_buffer_format.type==HAILO_FORMAT_TYPE_FLOAT32);
  assert(in->raw_buffer.size==16 && out->raw_buffer.size==16); CHECKED();
  if(infer_fail)return HAILO_TIMEOUT;
  float* x=in->raw_buffer.buffer; float* y=out->raw_buffer.buffer;
  for(int i=0;i<4;++i)y[i]=fmaxf(0,x[i]);
  if(nonfinite_output)y[3]=NAN;
  return HAILO_SUCCESS;
}
static void rejected(long long h,double* x,long long in_n,long long out_n){
  double y[4]={41,42,43,44};
  assert(tn_npu_model_run_f64_1d(h,x,in_n,y,out_n)==-1);
  for(int i=0;i<4;++i)assert(y[i]==41+i);
  assert(!active_count);
}
int main(void){
  has_device=0;
  assert(!tn_npu_has_backend() && !tn_npu_device_count() && tn_npu_init()==-1);
  assert(tn_npu_set_device(0)==-1 && tn_npu_model_load((unsigned char*)"model.hef")==-1);
  assert(!tn_npu_shutdown() && !live);
  has_device=1; scan_bad_name=1; assert(!tn_npu_has_backend()); scan_bad_name=0;
  assert(tn_npu_device_count()==2 && !tn_npu_set_device(1));
  assert(tn_npu_set_device(-1)==-1 && tn_npu_set_device(2)==-1 && !tn_npu_init());
  unsigned char* name=tn_npu_backend_name(); assert(!strcmp((char*)name,"hailort-npu")); free(name);
  assert(tn_npu_model_load(NULL)==-1 && tn_npu_model_load((unsigned char*)"")==-1);
  int* flags[]={&wrong_physical,&multiple_groups,&multiple_inputs,&multiple_outputs,&bad_bytes};
  for(size_t i=0;i<sizeof(flags)/sizeof(*flags);++i){
    *flags[i]=1; assert(tn_npu_model_load((unsigned char*)"model.hef")==-1 && !live); *flags[i]=0;
  }
  for(bad_info=1;bad_info<=6;++bad_info)assert(tn_npu_model_load((unsigned char*)"model.hef")==-1 && !live);
  bad_info=0;
  calls=0; long long h=tn_npu_model_load((unsigned char*)"model.hef"); int load_calls=calls;
  assert(h>0 && live==2 && tn_npu_set_device(0)==-1);
  assert(!tn_npu_model_free(h) && tn_npu_model_free(h)==-1 && !live);
  for(int i=1;i<=load_calls;++i){
    calls=0; fail_call=i; assert(tn_npu_model_load((unsigned char*)"model.hef")==-1 && !live);
  }
  fail_call=0; h=tn_npu_model_load((unsigned char*)"model.hef"); assert(h>0);
  double x[4]={-3,0,1.25,4}, y[4]={0};
  calls=0; assert(!tn_npu_model_run_f64_1d(h,x,4,y,4)); int run_calls=calls;
  assert(y[0]==0 && y[1]==0 && y[2]==1.25 && y[3]==4 && live==2);
  for(int i=1;i<=run_calls;++i){ calls=0; fail_call=i; rejected(h,x,4,4); assert(live==2); }
  fail_call=0;
  rejected(h,x,3,4); rejected(h,x,4,3); rejected(h,NULL,4,4); rejected(h,x,LLONG_MAX,4);
  x[0]=NAN; rejected(h,x,4,4); x[0]=INFINITY; rejected(h,x,4,4);
  x[0]=DBL_MAX; rejected(h,x,4,4); x[0]=-3;
  infer_fail=1; rejected(h,x,4,4); infer_fail=0;
  nonfinite_output=1; rejected(h,x,4,4); nonfinite_output=0;
  assert(!tn_npu_model_run_f64_1d(h,x,4,x,4) && x[0]==0 && x[2]==1.25);
  assert(!tn_npu_alloc(16) && tn_npu_memset(NULL,0,16)==-1 && !tn_npu_supports_dxil());
  assert(!tn_npu_shutdown() && !live); rejected(h,x,4,4);
  long long next=tn_npu_model_load((unsigned char*)"model.hef"); assert(next>h);
  assert(!tn_npu_model_free(next) && !tn_npu_shutdown() && !live);
  printf("PASS Hailo official-header provider contract; %d load and %d run failure stages; NOT device execution\n",load_calls,run_calls);
  return 0;
}
