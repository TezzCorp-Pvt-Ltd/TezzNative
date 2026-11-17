/* Dedicated Hailo model inference through the official HailoRT C API.
 * Explicitly enabled builds only. HEF, one network/input/output, fixed f32 host
 * tensors; no CPU/GPU fallback and no generic shader or raw-device-buffer API.
 */
#pragma once
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <hailo/hailort.h>
#ifdef _WIN32
#include <windows.h>
static SRWLOCK tn_hailo_mutex = SRWLOCK_INIT;
#define TN_HAILO_LOCK() AcquireSRWLockExclusive(&tn_hailo_mutex)
#define TN_HAILO_UNLOCK() ReleaseSRWLockExclusive(&tn_hailo_mutex)
#else
#include <pthread.h>
static pthread_mutex_t tn_hailo_mutex = PTHREAD_MUTEX_INITIALIZER;
#define TN_HAILO_LOCK() pthread_mutex_lock(&tn_hailo_mutex)
#define TN_HAILO_UNLOCK() pthread_mutex_unlock(&tn_hailo_mutex)
#endif

typedef struct tn_hailo_model {
  long long id;
  hailo_hef hef;
  hailo_vdevice device;
  hailo_configured_network_group network;
  hailo_input_vstream_params_by_name_t input;
  hailo_output_vstream_params_by_name_t output;
  size_t input_bytes, output_bytes;
  struct tn_hailo_model* next;
} tn_hailo_model;

static tn_hailo_model* tn_hailo_models;
static long long tn_hailo_next_id = 1;
static hailo_device_id_t tn_hailo_selected;

static long long tn_hailo_devices(long long select){
  hailo_device_id_t devices[64] = {0};
  size_t count = 64;
  if(hailo_scan_devices(NULL, devices, &count) != HAILO_SUCCESS || count > 64) return 0;
  for(size_t i = 0; i < count; ++i)
    if(!devices[i].id[0] || !memchr(devices[i].id, 0, sizeof(devices[i].id))) return 0;
  if(select >= 0 && (uint64_t)select < count) tn_hailo_selected = devices[select];
  return (long long)count;
}

static int tn_hailo_ready(void){
  return tn_hailo_selected.id[0] || tn_hailo_devices(0) > 0;
}

static void tn_hailo_release(tn_hailo_model* model){
  if(!model) return;
  /* Configured groups belong to their vdevice and have no separate release API. */
  if(model->device) (void)hailo_release_vdevice(model->device);
  if(model->hef) (void)hailo_release_hef(model->hef);
  free(model);
}

static tn_hailo_model* tn_hailo_find(long long id){
  for(tn_hailo_model* model = tn_hailo_models; model; model = model->next)
    if(model->id == id) return model;
  return NULL;
}

static int tn_hailo_port(hailo_vstream_info_t* info, char* name,
                         hailo_vstream_params_t* params, size_t* bytes){
  if(!memchr(info->name, 0, sizeof(info->name)) || !memchr(name, 0, HAILO_MAX_STREAM_NAME_SIZE) ||
     !info->name[0] || strcmp(info->name, name)) return 0;
  /* NMS, packed image and hardware-specific layouts are not flat f32 tensors. */
  if(info->format.order != HAILO_FORMAT_ORDER_NHWC && info->format.order != HAILO_FORMAT_ORDER_NCHW &&
     info->format.order != HAILO_FORMAT_ORDER_NC) return 0;
  size_t count = 1;
  const uint32_t dims[] = {info->shape.height, info->shape.width, info->shape.features};
  for(int i = 0; i < 3; ++i){
    if(!dims[i] || dims[i] > SIZE_MAX / sizeof(double) / count) return 0;
    count *= dims[i];
  }
  params->user_buffer_format.type = HAILO_FORMAT_TYPE_FLOAT32;
  params->user_buffer_format.order = info->format.order;
  params->user_buffer_format.flags = HAILO_FORMAT_FLAGS_NONE;
  params->timeout_ms = HAILO_DEFAULT_VSTREAM_TIMEOUT_MS;
  return hailo_get_vstream_frame_size(info, &params->user_buffer_format, bytes) == HAILO_SUCCESS &&
         *bytes == count * sizeof(float);
}

TN_EXPORT long long tn_npu_has_backend(void){
  TN_HAILO_LOCK();
  long long result = tn_hailo_devices(-1) > 0;
  TN_HAILO_UNLOCK();
  return result;
}
TN_EXPORT long long tn_npu_device_count(void){
  TN_HAILO_LOCK();
  long long result = tn_hailo_devices(-1);
  TN_HAILO_UNLOCK();
  return result;
}
TN_EXPORT long long tn_npu_init(void){
  TN_HAILO_LOCK();
  long long result = tn_hailo_ready() ? 0 : -1;
  TN_HAILO_UNLOCK();
  return result;
}
TN_EXPORT long long tn_npu_set_device(long long id){
  if(id < 0) return -1;
  TN_HAILO_LOCK();
  long long result = !tn_hailo_models && tn_hailo_devices(id) > id ? 0 : -1;
  TN_HAILO_UNLOCK();
  return result;
}
TN_EXPORT long long tn_npu_shutdown(void){
  TN_HAILO_LOCK();
  while(tn_hailo_models){
    tn_hailo_model* next = tn_hailo_models->next;
    tn_hailo_release(tn_hailo_models);
    tn_hailo_models = next;
  }
  memset(&tn_hailo_selected, 0, sizeof(tn_hailo_selected));
  /* Model IDs are not recycled across shutdown/reinitialization. */
  TN_HAILO_UNLOCK();
  return 0;
}
TN_EXPORT unsigned char* tn_npu_backend_name(void){
  return tn_dup_cstr(tn_npu_has_backend() ? "hailort-npu" : "none");
}
TN_EXPORT long long tn_npu_model_load(unsigned char* path){
  tn_hailo_model* model = NULL;
  hailo_vdevice_params_t params = {0};
  hailo_configure_params_t* config = NULL;
  hailo_device_id_t physical = {0};
  hailo_vstream_info_t infos[2] = {0};
  size_t groups = 1, inputs = 1, outputs = 1, info_count = 2, physical_count = 1;
  long long result = -1;
  if(!path || !*path) return -1;
  TN_HAILO_LOCK();
  if(!tn_hailo_ready() || tn_hailo_next_id == LLONG_MAX) goto done;
  model = (tn_hailo_model*)calloc(1, sizeof(*model));
  config = (hailo_configure_params_t*)calloc(1, sizeof(*config));
  if(!model || !config || hailo_create_hef_file(&model->hef, (const char*)path) != HAILO_SUCCESS ||
     hailo_init_vdevice_params(&params) != HAILO_SUCCESS) goto done;
  params.device_count = 1;
  params.device_ids = &tn_hailo_selected;
  params.scheduling_algorithm = HAILO_SCHEDULING_ALGORITHM_NONE;
  params.group_id = HAILO_UNIQUE_VDEVICE_GROUP_ID;
  params.multi_process_service = false;
  if(hailo_create_vdevice(&params, &model->device) != HAILO_SUCCESS ||
     hailo_vdevice_get_physical_devices_ids(model->device, &physical, &physical_count) != HAILO_SUCCESS ||
     physical_count != 1 || !memchr(physical.id, 0, sizeof(physical.id)) ||
     strcmp(physical.id, tn_hailo_selected.id) ||
     hailo_init_configure_params_by_vdevice(model->hef, model->device, config) != HAILO_SUCCESS ||
     config->network_group_params_count != 1) goto done;
  config->network_group_params[0].batch_size = 1;
  if(hailo_configure_vdevice(model->device, model->hef, config, &model->network, &groups) != HAILO_SUCCESS ||
     groups != 1 || !model->network ||
     hailo_make_input_vstream_params(model->network, false, HAILO_FORMAT_TYPE_FLOAT32, &model->input, &inputs) != HAILO_SUCCESS ||
     inputs != 1 ||
     hailo_make_output_vstream_params(model->network, false, HAILO_FORMAT_TYPE_FLOAT32, &model->output, &outputs) != HAILO_SUCCESS ||
     outputs != 1 ||
     hailo_hef_get_all_vstream_infos(model->hef, NULL, infos, &info_count) != HAILO_SUCCESS || info_count != 2) goto done;
  int seen_input = 0, seen_output = 0;
  for(size_t i = 0; i < 2; ++i){
    if(infos[i].direction == HAILO_H2D_STREAM && !seen_input){
      if(!tn_hailo_port(&infos[i], model->input.name, &model->input.params, &model->input_bytes)) goto done;
      seen_input = 1;
    } else if(infos[i].direction == HAILO_D2H_STREAM && !seen_output){
      if(!tn_hailo_port(&infos[i], model->output.name, &model->output.params, &model->output_bytes)) goto done;
      seen_output = 1;
    } else goto done;
  }
  if(!seen_input || !seen_output) goto done;
  result = model->id = tn_hailo_next_id++;
  model->next = tn_hailo_models;
  tn_hailo_models = model;
  model = NULL;
done:
  free(config);
  tn_hailo_release(model);
  TN_HAILO_UNLOCK();
  return result;
}
TN_EXPORT long long tn_npu_model_free(long long id){
  if(id <= 0) return -1;
  TN_HAILO_LOCK();
  tn_hailo_model** link = &tn_hailo_models;
  while(*link && (*link)->id != id) link = &(*link)->next;
  long long result = -1;
  if(*link){
    tn_hailo_model* model = *link;
    *link = model->next;
    tn_hailo_release(model);
    result = 0;
  }
  TN_HAILO_UNLOCK();
  return result;
}
TN_EXPORT long long tn_npu_model_run_f64_1d(long long id, double* input, long long in_n,
                                          double* output, long long out_n){
  if(id <= 0 || !input || !output || in_n <= 0 || out_n <= 0 ||
     (uint64_t)in_n > SIZE_MAX / sizeof(double) || (uint64_t)out_n > SIZE_MAX / sizeof(double)) return -1;
  long long result = -1;
  float* in_data = NULL;
  float* out_data = NULL;
  hailo_activated_network_group active = NULL;
  hailo_stream_raw_buffer_by_name_t in = {0}, out = {0};
  TN_HAILO_LOCK();
  tn_hailo_model* model = tn_hailo_find(id);
  if(!model || (uint64_t)in_n != model->input_bytes/sizeof(float) ||
     (uint64_t)out_n != model->output_bytes/sizeof(float)) goto done;
  for(long long i = 0; i < in_n; ++i)
    if(!isfinite(input[i]) || input[i] > FLT_MAX || input[i] < -FLT_MAX) goto done;
  in_data = (float*)malloc(model->input_bytes);
  out_data = (float*)malloc(model->output_bytes);
  if(!in_data || !out_data) goto done;
  for(long long i = 0; i < in_n; ++i) in_data[i] = (float)input[i];
  memcpy(in.name, model->input.name, sizeof(in.name));
  memcpy(out.name, model->output.name, sizeof(out.name));
  in.raw_buffer.buffer = in_data; in.raw_buffer.size = model->input_bytes;
  out.raw_buffer.buffer = out_data; out.raw_buffer.size = model->output_bytes;
  if(hailo_activate_network_group(model->network, NULL, &active) != HAILO_SUCCESS || !active) goto done;
  hailo_status status = hailo_infer(model->network, &model->input, &in, 1, &model->output, &out, 1, 1);
  hailo_status deactivated = hailo_deactivate_network_group(active);
  active = NULL;
  if(status != HAILO_SUCCESS || deactivated != HAILO_SUCCESS) goto done;
  for(long long i = 0; i < out_n; ++i) if(!isfinite(out_data[i])) goto done;
  for(long long i = 0; i < out_n; ++i) output[i] = out_data[i];
  result = 0;
done:
  if(active) (void)hailo_deactivate_network_group(active);
  free(out_data);
  free(in_data);
  TN_HAILO_UNLOCK();
  return result;
}
#undef TN_HAILO_LOCK
#undef TN_HAILO_UNLOCK
