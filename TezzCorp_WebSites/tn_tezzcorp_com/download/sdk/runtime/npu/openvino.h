/* Intel NPU model inference through the official OpenVINO C API.
 * Included by tnrt.c only in an explicitly enabled OpenVINO build.
 * No AUTO/HETERO/CPU/GPU device selection is permitted here.
 */
#pragma once
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
static SRWLOCK tn_ov_mutex = SRWLOCK_INIT;
#define TN_OV_LOCK() AcquireSRWLockExclusive(&tn_ov_mutex)
#define TN_OV_UNLOCK() ReleaseSRWLockExclusive(&tn_ov_mutex)
#else
#include <pthread.h>
static pthread_mutex_t tn_ov_mutex = PTHREAD_MUTEX_INITIALIZER;
#define TN_OV_LOCK() pthread_mutex_lock(&tn_ov_mutex)
#define TN_OV_UNLOCK() pthread_mutex_unlock(&tn_ov_mutex)
#endif
/* Windows SSPI also declares STRING; keep vendor enum names local. */
#define STRING TN_OV_STRING
#include <openvino/c/openvino.h>
#undef STRING
#undef BOOLEAN

typedef struct tn_ov_model {
  long long id;
  ov_compiled_model_t* compiled;
  ov_infer_request_t* request;
  struct tn_ov_model* next;
} tn_ov_model;

static ov_core_t* tn_ov_core;
static tn_ov_model* tn_ov_models;
static long long tn_ov_next_id = 1;
static char tn_ov_selected[64];

static int tn_ov_device_name(const char* name){
  if(!name || strncmp(name, "NPU", 3)) return 0;
  name += 3;
  if(!*name) return 1;
  if(*name++ != '.' || !*name) return 0;
  for(; *name; ++name) if(*name < '0' || *name > '9') return 0;
  return 1;
}

/* C API serializes EXECUTION_DEVICES as a string. Accept one NPU only. */
static int tn_ov_execution_device(const char* value){
  char name[64];
  size_t n = 0;
  if(!value) return 0;
  while(*value == ' ' || *value == '\t' || *value == '[') ++value;
  while(*value && *value != ' ' && *value != '\t' && *value != ']'){
    if(n + 1 >= sizeof(name)) return 0;
    name[n++] = *value++;
  }
  name[n] = 0;
  while(*value == ' ' || *value == '\t' || *value == ']') ++value;
  return !*value && tn_ov_device_name(name);
}

static long long tn_ov_devices(long long select){
  ov_available_devices_t devices = {0};
  long long count = 0;
  char chosen[sizeof(tn_ov_selected)] = {0};
  if(!tn_ov_core && ov_core_create(&tn_ov_core) != OK) return 0;
  if(ov_core_get_available_devices(tn_ov_core, &devices) != OK) return 0;
  for(size_t i = 0; i < devices.size; ++i){
    const char* name = devices.devices[i];
    if(!tn_ov_device_name(name) || strlen(name) >= sizeof(chosen)) continue;
    if(count == select) memcpy(chosen, name, strlen(name) + 1);
    ++count;
  }
  ov_available_devices_free(&devices);
  if(select >= 0 && chosen[0]) memcpy(tn_ov_selected, chosen, sizeof(chosen));
  return count;
}

static int tn_ov_ready(void){
  if(tn_ov_selected[0]) return 1;
  return tn_ov_devices(0) > 0 && tn_ov_selected[0];
}

static void tn_ov_release(tn_ov_model* model){
  if(!model) return;
  if(model->request) ov_infer_request_free(model->request);
  if(model->compiled) ov_compiled_model_free(model->compiled);
  free(model);
}

static tn_ov_model* tn_ov_find(long long id){
  for(tn_ov_model* model = tn_ov_models; model; model = model->next)
    if(model->id == id) return model;
  return NULL;
}

/* Static f32 model ports only. The public API flattens their fixed shape. */
static int tn_ov_buffer(ov_tensor_t* tensor, long long expected, float** data){
  size_t size = 0, bytes = 0;
  ov_element_type_e type = UNDEFINED;
  void* memory = NULL;
  if(!tensor || expected <= 0 || (uint64_t)expected > SIZE_MAX / sizeof(float)) return 0;
  if(ov_tensor_get_element_type(tensor, &type) != OK || type != F32) return 0;
  if(ov_tensor_get_size(tensor, &size) != OK || size != (uint64_t)expected) return 0;
  if(ov_tensor_get_byte_size(tensor, &bytes) != OK || bytes != size * sizeof(float)) return 0;
  if(ov_tensor_data(tensor, &memory) != OK || !memory) return 0;
  *data = (float*)memory;
  return 1;
}

TN_EXPORT long long tn_npu_has_backend(void){
  TN_OV_LOCK();
  long long result = tn_ov_devices(-1) > 0;
  TN_OV_UNLOCK();
  return result;
}
TN_EXPORT long long tn_npu_device_count(void){
  TN_OV_LOCK();
  long long result = tn_ov_devices(-1);
  TN_OV_UNLOCK();
  return result;
}
TN_EXPORT long long tn_npu_init(void){
  TN_OV_LOCK();
  long long result = tn_ov_ready() ? 0 : -1;
  TN_OV_UNLOCK();
  return result;
}
TN_EXPORT long long tn_npu_set_device(long long id){
  if(id < 0) return -1;
  TN_OV_LOCK();
  long long result = -1;
  if(!tn_ov_models && tn_ov_devices(id) > id) result = 0;
  TN_OV_UNLOCK();
  return result;
}
TN_EXPORT long long tn_npu_shutdown(void){
  TN_OV_LOCK();
  while(tn_ov_models){
    tn_ov_model* next = tn_ov_models->next;
    tn_ov_release(tn_ov_models);
    tn_ov_models = next;
  }
  if(tn_ov_core) ov_core_free(tn_ov_core);
  tn_ov_core = NULL;
  tn_ov_selected[0] = 0;
  /* IDs are never recycled, including across shutdown/reinitialization. */
  TN_OV_UNLOCK();
  return 0;
}
TN_EXPORT unsigned char* tn_npu_backend_name(void){
  return tn_dup_cstr(tn_npu_has_backend() ? "openvino-npu" : "none");
}
TN_EXPORT long long tn_npu_model_load(unsigned char* path){
  ov_model_t* graph = NULL;
  ov_output_const_port_t* port = NULL;
  ov_shape_t shape = {0};
  tn_ov_model* model = NULL;
  char* execution = NULL;
  size_t inputs = 0, outputs = 0;
  long long result = -1;
  if(!path || !*path) return -1;
  TN_OV_LOCK();
  if(!tn_ov_ready() || tn_ov_next_id == LLONG_MAX) goto done;
  model = (tn_ov_model*)calloc(1, sizeof(*model));
  if(!model || ov_core_read_model(tn_ov_core, (const char*)path, NULL, &graph) != OK) goto done;
  /* The explicit selected NPU is the sole compilation/execution target. */
  if(ov_core_compile_model(tn_ov_core, graph, tn_ov_selected, 0, &model->compiled) != OK) goto done;
  if(ov_compiled_model_get_property(model->compiled, "EXECUTION_DEVICES", &execution) != OK ||
     !tn_ov_execution_device(execution)) goto done;
  if(ov_compiled_model_inputs_size(model->compiled, &inputs) != OK || inputs != 1 ||
     ov_compiled_model_outputs_size(model->compiled, &outputs) != OK || outputs != 1) goto done;
  for(int output = 0; output < 2; ++output){
    ov_element_type_e type = UNDEFINED;
    ov_status_e status = output ? ov_compiled_model_output(model->compiled, &port)
                                : ov_compiled_model_input(model->compiled, &port);
    if(status != OK || ov_port_get_element_type(port, &type) != OK || type != F32 ||
       ov_const_port_get_shape(port, &shape) != OK || shape.rank < 1 || shape.rank > 8) goto done;
    size_t count = 1;
    for(int64_t axis = 0; axis < shape.rank; ++axis){
      if(shape.dims[axis] <= 0 || (uint64_t)shape.dims[axis] > SIZE_MAX / sizeof(float) / count) goto done;
      count *= (size_t)shape.dims[axis];
    }
    ov_shape_free(&shape);
    memset(&shape, 0, sizeof(shape));
    ov_output_const_port_free(port);
    port = NULL;
  }
  if(ov_compiled_model_create_infer_request(model->compiled, &model->request) != OK) goto done;
  result = model->id = tn_ov_next_id++;
  model->next = tn_ov_models;
  tn_ov_models = model;
  model = NULL;
done:
  if(shape.dims) ov_shape_free(&shape);
  if(port) ov_output_const_port_free(port);
  if(execution) ov_free(execution);
  if(graph) ov_model_free(graph);
  tn_ov_release(model);
  TN_OV_UNLOCK();
  return result;
}
TN_EXPORT long long tn_npu_model_free(long long id){
  if(id <= 0) return -1;
  TN_OV_LOCK();
  tn_ov_model** link = &tn_ov_models;
  while(*link && (*link)->id != id) link = &(*link)->next;
  long long result = -1;
  if(*link){
    tn_ov_model* model = *link;
    *link = model->next;
    tn_ov_release(model);
    result = 0;
  }
  TN_OV_UNLOCK();
  return result;
}
TN_EXPORT long long tn_npu_model_run_f64_1d(long long id, double* input, long long in_n, double* output, long long out_n){
  if(id <= 0 || !input || !output || in_n <= 0 || out_n <= 0) return -1;
  if((uint64_t)in_n > SIZE_MAX / sizeof(double) || (uint64_t)out_n > SIZE_MAX / sizeof(double)) return -1;
  ov_tensor_t* in = NULL;
  ov_tensor_t* out = NULL;
  float* in_data = NULL;
  float* out_data = NULL;
  long long result = -1;
  TN_OV_LOCK();
  tn_ov_model* model = tn_ov_find(id);
  if(!model || ov_infer_request_get_input_tensor(model->request, &in) != OK ||
     ov_infer_request_get_output_tensor(model->request, &out) != OK ||
     !tn_ov_buffer(in, in_n, &in_data) || !tn_ov_buffer(out, out_n, &out_data)) goto done;
  for(long long i = 0; i < in_n; ++i)
    if(!isfinite(input[i]) || input[i] > FLT_MAX || input[i] < -FLT_MAX) goto done;
  for(long long i = 0; i < in_n; ++i) in_data[i] = (float)input[i];
  if(ov_infer_request_infer(model->request) != OK) goto done;
  /* Read the current result: the provider may replace its output storage. */
  ov_tensor_free(out);
  out = NULL;
  if(ov_infer_request_get_output_tensor(model->request, &out) != OK ||
     !tn_ov_buffer(out, out_n, &out_data)) goto done;
  /* Check all results before modifying any caller-owned output. */
  for(long long i = 0; i < out_n; ++i) if(!isfinite(out_data[i])) goto done;
  for(long long i = 0; i < out_n; ++i) output[i] = out_data[i];
  result = 0;
done:
  if(in) ov_tensor_free(in);
  if(out) ov_tensor_free(out);
  TN_OV_UNLOCK();
  return result;
}
#undef TN_OV_LOCK
#undef TN_OV_UNLOCK
