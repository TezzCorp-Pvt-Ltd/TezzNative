/* Dedicated AMD/Qualcomm plugin EPs through the ORT 1.23 C API.
 * Only enumerated NPU hardware may be selected; CPU fallback is disabled.
 * These are model providers, not raw memory/compute-kernel providers.
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
static SRWLOCK tn_ort_mutex = SRWLOCK_INIT;
#define TN_ORT_LOCK() AcquireSRWLockExclusive(&tn_ort_mutex)
#define TN_ORT_UNLOCK() ReleaseSRWLockExclusive(&tn_ort_mutex)
#else
#include <pthread.h>
static pthread_mutex_t tn_ort_mutex = PTHREAD_MUTEX_INITIALIZER;
#define TN_ORT_LOCK() pthread_mutex_lock(&tn_ort_mutex)
#define TN_ORT_UNLOCK() pthread_mutex_unlock(&tn_ort_mutex)
#endif
#include <onnxruntime_c_api.h>
#if ORT_API_VERSION < 23
#error Dedicated NPU adapters require ONNX Runtime C API 1.23 or later
#endif
#if defined(TN_NPU_ORT_AMD)
#define TN_ORT_PROVIDER "VitisAIExecutionProvider"
#define TN_ORT_NAME "amd-vitis-npu"
#elif defined(TN_NPU_ORT_QNN)
#define TN_ORT_PROVIDER "QNNExecutionProvider"
#define TN_ORT_NAME "qualcomm-htp-npu"
#else
#error Select a dedicated ORT NPU provider
#endif

typedef struct tn_ort_model {
  long long id;
  OrtSession* session;
  char* names[2];
  int64_t shapes[2][8];
  size_t ranks[2], counts[2];
  struct tn_ort_model* next;
} tn_ort_model;
static const OrtApi* tn_ort_api;
static OrtEnv* tn_ort_env;
static OrtAllocator* tn_ort_allocator;
static tn_ort_model* tn_ort_models;
static long long tn_ort_selected, tn_ort_next_id = 1;

static int tn_ort_ok(OrtStatus* status){
  if(!status) return 1;
  tn_ort_api->ReleaseStatus(status);
  return 0;
}
static ORTCHAR_T* tn_ort_path(const char* path){
  if(!path || !*path) return NULL;
#ifdef _WIN32
  int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
  if(count <= 0 || (size_t)count > SIZE_MAX / sizeof(wchar_t)) return NULL;
  wchar_t* out = (wchar_t*)malloc((size_t)count * sizeof(wchar_t));
  if(out && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, out, count)){
    free(out); out = NULL;
  }
  return out;
#else
  size_t count = strlen(path) + 1;
  char* out = (char*)malloc(count);
  if(out) memcpy(out, path, count);
  return out;
#endif
}
static int tn_ort_start(void){
  if(tn_ort_env) return 1;
  tn_ort_api = OrtGetApiBase()->GetApi(23);
  if(!tn_ort_api) return 0;
  if(!tn_ort_ok(tn_ort_api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "TezzNative-NPU", &tn_ort_env))) return 0;
  if(!tn_ort_ok(tn_ort_api->DisableTelemetryEvents(tn_ort_env)) ||
     !tn_ort_ok(tn_ort_api->GetAllocatorWithDefaultOptions(&tn_ort_allocator))) goto failed;
  /* A local vendor plugin is optional when the runtime already registers it.
   * Never download or auto-select another provider. Read once per lifecycle. */
  const char* plugin = getenv("TN_NPU_EP_LIBRARY");
  if(plugin && *plugin){
#ifdef _WIN32
    if(!((strlen(plugin) > 3 && plugin[1] == ':' && (plugin[2] == '\\' || plugin[2] == '/')) ||
         (plugin[0] == '\\' && plugin[1] == '\\'))) goto failed;
#else
    if(plugin[0] != '/') goto failed;
#endif
    ORTCHAR_T* path = tn_ort_path(plugin);
    if(!path) goto failed;
    int ok = tn_ort_ok(tn_ort_api->RegisterExecutionProviderLibrary(tn_ort_env, TN_ORT_NAME, path));
    free(path);
    if(!ok) goto failed;
  }
  return 1;
failed:
  tn_ort_api->ReleaseEnv(tn_ort_env);
  tn_ort_env = NULL; tn_ort_allocator = NULL;
  return 0;
}
static int tn_ort_is_device(const OrtEpDevice* device){
  if(!device) return 0;
  const char* name = tn_ort_api->EpDevice_EpName(device);
  const OrtHardwareDevice* hardware = tn_ort_api->EpDevice_Device(device);
  return name && !strcmp(name, TN_ORT_PROVIDER) && hardware &&
         tn_ort_api->HardwareDevice_Type(hardware) == OrtHardwareDeviceType_NPU;
}
static long long tn_ort_devices(long long index, const OrtEpDevice** selected){
  const OrtEpDevice* const* devices = NULL;
  size_t count = 0;
  long long matches = 0;
  if(selected) *selected = NULL;
  if(!tn_ort_start() || !tn_ort_ok(tn_ort_api->GetEpDevices(tn_ort_env, &devices, &count))) return 0;
  for(size_t i = 0; i < count; ++i){
    if(!tn_ort_is_device(devices[i])) continue;
    if(matches == index && selected) *selected = devices[i];
    if(matches == LLONG_MAX) return 0;
    ++matches;
  }
  return matches;
}
static void tn_ort_release(tn_ort_model* model){
  if(!model) return;
  if(model->session) tn_ort_api->ReleaseSession(model->session);
  for(int i = 0; i < 2; ++i)
    if(model->names[i]) tn_ort_allocator->Free(tn_ort_allocator, model->names[i]);
  free(model);
}
static tn_ort_model* tn_ort_find(long long id){
  for(tn_ort_model* m = tn_ort_models; m; m = m->next) if(m->id == id) return m;
  return NULL;
}
TN_EXPORT long long tn_npu_device_count(void){
  TN_ORT_LOCK(); long long result = tn_ort_devices(-1, NULL); TN_ORT_UNLOCK(); return result;
}
TN_EXPORT long long tn_npu_has_backend(void){ return tn_npu_device_count() > 0; }
TN_EXPORT unsigned char* tn_npu_backend_name(void){
  return tn_dup_cstr(tn_npu_has_backend() ? TN_ORT_NAME : "none");
}
TN_EXPORT long long tn_npu_init(void){
  TN_ORT_LOCK();
  long long result = tn_ort_devices(-1, NULL) > tn_ort_selected ? 0 : -1;
  TN_ORT_UNLOCK(); return result;
}
TN_EXPORT long long tn_npu_set_device(long long id){
  if(id < 0) return -1;
  TN_ORT_LOCK(); long long result = -1;
  if(!tn_ort_models && tn_ort_devices(-1, NULL) > id){ tn_ort_selected = id; result = 0; }
  TN_ORT_UNLOCK(); return result;
}
TN_EXPORT long long tn_npu_shutdown(void){
  TN_ORT_LOCK();
  while(tn_ort_models){ tn_ort_model* next = tn_ort_models->next; tn_ort_release(tn_ort_models); tn_ort_models = next; }
  if(tn_ort_env) tn_ort_api->ReleaseEnv(tn_ort_env);
  tn_ort_env = NULL; tn_ort_allocator = NULL; tn_ort_selected = 0;
  TN_ORT_UNLOCK(); return 0;
}
TN_EXPORT long long tn_npu_model_load(unsigned char* filename){
  ORTCHAR_T* path = tn_ort_path((const char*)filename);
  if(!path) return -1;
  OrtSessionOptions* options = NULL;
  OrtTypeInfo* info = NULL;
  const OrtEpDevice* device = NULL;
  const OrtEpDevice* assigned = NULL;
  tn_ort_model* model = NULL;
  long long result = -1;
  TN_ORT_LOCK();
  if(tn_ort_next_id == LLONG_MAX || !tn_ort_devices(tn_ort_selected, &device) || !device) goto done;
  model = (tn_ort_model*)calloc(1, sizeof(*model));
  if(!model || !tn_ort_ok(tn_ort_api->CreateSessionOptions(&options)) ||
     !tn_ort_ok(tn_ort_api->AddSessionConfigEntry(options, "session.disable_cpu_ep_fallback", "1"))) goto done;
#if defined(TN_NPU_ORT_QNN)
  const char* keys[] = {"backend_type", "offload_graph_io_quantization"};
  const char* values[] = {"htp", "0"};
  if(!tn_ort_ok(tn_ort_api->SessionOptionsAppendExecutionProvider_V2(options, tn_ort_env, &device, 1, keys, values, 2))) goto done;
#else
  if(!tn_ort_ok(tn_ort_api->SessionOptionsAppendExecutionProvider_V2(options, tn_ort_env, &device, 1, NULL, NULL, 0))) goto done;
#endif
  if(!tn_ort_ok(tn_ort_api->CreateSession(tn_ort_env, path, options, &model->session))) goto done;
  size_t count = 0;
  if(!tn_ort_ok(tn_ort_api->SessionGetInputCount(model->session, &count)) || count != 1 ||
     !tn_ort_ok(tn_ort_api->SessionGetOutputCount(model->session, &count)) || count != 1 ||
     !tn_ort_ok(tn_ort_api->SessionGetEpDeviceForInputs(model->session, &assigned, 1)) ||
     assigned != device || !tn_ort_is_device(assigned)) goto done;
  for(int output = 0; output < 2; ++output){
    const OrtTensorTypeAndShapeInfo* tensor = NULL;
    ONNXTensorElementDataType type;
    OrtStatus* status = output ? tn_ort_api->SessionGetOutputTypeInfo(model->session, 0, &info)
                              : tn_ort_api->SessionGetInputTypeInfo(model->session, 0, &info);
    if(!tn_ort_ok(status) || !tn_ort_ok(tn_ort_api->CastTypeInfoToTensorInfo(info, &tensor)) || !tensor ||
       !tn_ort_ok(tn_ort_api->GetTensorElementType(tensor, &type)) || type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
       !tn_ort_ok(tn_ort_api->GetDimensionsCount(tensor, &model->ranks[output])) ||
       model->ranks[output] < 1 || model->ranks[output] > 8 ||
       !tn_ort_ok(tn_ort_api->GetDimensions(tensor, model->shapes[output], model->ranks[output]))) goto done;
    size_t elements = 1;
    for(size_t i = 0; i < model->ranks[output]; ++i){
      int64_t dim = model->shapes[output][i];
      if(dim <= 0 || (uint64_t)dim > SIZE_MAX / sizeof(double) / elements) goto done;
      elements *= (size_t)dim;
    }
    model->counts[output] = elements;
    tn_ort_api->ReleaseTypeInfo(info); info = NULL;
    status = output ? tn_ort_api->SessionGetOutputName(model->session, 0, tn_ort_allocator, &model->names[output])
                    : tn_ort_api->SessionGetInputName(model->session, 0, tn_ort_allocator, &model->names[output]);
    if(!tn_ort_ok(status) || !model->names[output] || !*model->names[output]) goto done;
  }
  result = model->id = tn_ort_next_id++;
  model->next = tn_ort_models; tn_ort_models = model; model = NULL;
done:
  if(info) tn_ort_api->ReleaseTypeInfo(info);
  if(options) tn_ort_api->ReleaseSessionOptions(options);
  tn_ort_release(model);
  TN_ORT_UNLOCK(); free(path); return result;
}
TN_EXPORT long long tn_npu_model_free(long long id){
  if(id <= 0) return -1;
  TN_ORT_LOCK();
  tn_ort_model** link = &tn_ort_models;
  while(*link && (*link)->id != id) link = &(*link)->next;
  long long result = -1;
  if(*link){ tn_ort_model* m = *link; *link = m->next; tn_ort_release(m); result = 0; }
  TN_ORT_UNLOCK(); return result;
}
TN_EXPORT long long tn_npu_model_run_f64_1d(long long id, double* input, long long in_n, double* output, long long out_n){
  if(id <= 0 || !input || !output || in_n <= 0 || out_n <= 0) return -1;
  OrtMemoryInfo* memory = NULL;
  OrtValue* values[2] = {NULL, NULL};
  float* buffers[2] = {NULL, NULL};
  long long result = -1;
  TN_ORT_LOCK();
  tn_ort_model* model = tn_ort_find(id);
  if(!model || (uint64_t)in_n != model->counts[0] || (uint64_t)out_n != model->counts[1]) goto done;
  for(long long i = 0; i < in_n; ++i)
    if(!isfinite(input[i]) || input[i] > FLT_MAX || input[i] < -FLT_MAX) goto done;
  /* Host buffers transfer model I/O; they are not a CPU execution provider. */
  if(!tn_ort_ok(tn_ort_api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory))) goto done;
  for(int i = 0; i < 2; ++i){
    size_t bytes = model->counts[i] * sizeof(float);
    buffers[i] = (float*)malloc(bytes);
    if(!buffers[i] || !tn_ort_ok(tn_ort_api->CreateTensorWithDataAsOrtValue(memory, buffers[i], bytes,
       model->shapes[i], model->ranks[i], ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &values[i]))) goto done;
  }
  for(long long i = 0; i < in_n; ++i) buffers[0][i] = (float)input[i];
  if(!tn_ort_ok(tn_ort_api->Run(model->session, NULL, (const char* const*)&model->names[0],
      (const OrtValue* const*)&values[0], 1, (const char* const*)&model->names[1], 1, &values[1]))) goto done;
  for(long long i = 0; i < out_n; ++i) if(!isfinite(buffers[1][i])) goto done;
  for(long long i = 0; i < out_n; ++i) output[i] = buffers[1][i];
  result = 0;
done:
  for(int i = 0; i < 2; ++i){ if(values[i]) tn_ort_api->ReleaseValue(values[i]); free(buffers[i]); }
  if(memory) tn_ort_api->ReleaseMemoryInfo(memory);
  TN_ORT_UNLOCK(); return result;
}
#undef TN_ORT_PROVIDER
#undef TN_ORT_NAME
#undef TN_ORT_LOCK
#undef TN_ORT_UNLOCK
