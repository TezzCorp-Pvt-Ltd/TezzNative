#pragma once

// ── GGUF Ingestion & Quantization Kernels ────────────────────────────────────

#define GGUF_MAGIC 0x46554747 // 'GGUF'
#define MAX_GGUF_TENSORS 4096
#define MAX_GGUF_NAME 128
#define MAX_GGUF_DIMS 8

typedef enum {
  GGUF_TYPE_F32  = 0,
  GGUF_TYPE_F16  = 1,
  GGUF_TYPE_Q4_0 = 2,
  GGUF_TYPE_Q4_1 = 3,
  GGUF_TYPE_Q5_0 = 6,
  GGUF_TYPE_Q5_1 = 7,
  GGUF_TYPE_Q8_0 = 8,
  GGUF_TYPE_Q8_1 = 9,
  GGUF_TYPE_I8   = 24,
  GGUF_TYPE_I16  = 25,
  GGUF_TYPE_I32  = 26,
  GGUF_TYPE_I64  = 27,
  GGUF_TYPE_F64  = 28,
  GGUF_TYPE_BF16 = 30
} GgufTensorType;

#pragma pack(push, 1)
typedef struct {
  uint16_t d;       // fp16 scale
  uint8_t  qs[16];  // 32 nibbles
} block_q4_0;

typedef struct {
  uint16_t d;       // fp16 scale
  int8_t   qs[32];  // 32 int8 weights
} block_q8_0;
#pragma pack(pop)

typedef struct {
  char name[MAX_GGUF_NAME];
  uint32_t type;
  int64_t shape[MAX_GGUF_DIMS];
  int ndim;
  int64_t numel;
  uint64_t offset;
  uint64_t size_bytes;
} GgufTensorMeta;

typedef struct {
  FILE* fp;
  uint32_t version;
  uint64_t tensor_count;
  uint64_t kv_count;
  uint64_t data_base_offset;
  uint64_t file_size;
  GgufTensorMeta* tensors;
  int tensor_n;
} GgufFile;

// Share the runtime's round-to-nearest, ties-to-even half conversions.
static inline float gguf_f16_to_f32(uint16_t h) { return tn_f16_to_f32(h); }
static inline uint16_t gguf_f32_to_f16(float val) { return tn_f32_to_f16(val); }

// Dequantize Q4_0 block array
TZ_CPU_EXPORT int tz_gguf_dequantize_q4_0(const void* src, float* dst, int64_t k) {
  if(!src || !dst || k <= 0) return -1;
  const block_q4_0* blocks = (const block_q4_0*)src;
  int64_t nb = k / 32;

  for(int64_t b = 0; b < nb; b++) {
    float d = gguf_f16_to_f32(blocks[b].d);
    float* out = dst + b * 32;
    for(int i = 0; i < 16; i++) {
      uint8_t q = blocks[b].qs[i];
      int8_t v0 = (int8_t)(q & 0x0F) - 8;
      int8_t v1 = (int8_t)(q >> 4)   - 8;
      out[i]      = (float)v0 * d;
      out[i + 16] = (float)v1 * d;
    }
  }
  return 0;
}

// Dequantize Q8_0 block array
TZ_CPU_EXPORT int tz_gguf_dequantize_q8_0(const void* src, float* dst, int64_t k) {
  if(!src || !dst || k <= 0) return -1;
  const block_q8_0* blocks = (const block_q8_0*)src;
  int64_t nb = k / 32;

  for(int64_t b = 0; b < nb; b++) {
    float d = gguf_f16_to_f32(blocks[b].d);
    float* out = dst + b * 32;
    for(int i = 0; i < 32; i++) {
      out[i] = (float)blocks[b].qs[i] * d;
    }
  }
  return 0;
}

// Quantize float array to Q4_0
TZ_CPU_EXPORT int tz_gguf_quantize_q4_0(const float* src, void* dst, int64_t k) {
  if(!src || !dst || k <= 0) return -1;
  block_q4_0* blocks = (block_q4_0*)dst;
  int64_t nb = k / 32;

  for(int64_t b = 0; b < nb; b++) {
    const float* in = src + b * 32;
    float max_val = 0.0f;
    for(int i = 0; i < 32; i++) {
      float abs_v = (float)fabs(in[i]);
      if(abs_v > max_val) max_val = abs_v;
    }
    float d = max_val / 7.0f;
    if(d == 0.0f) d = 1e-6f;
    float id = 1.0f / d;
    blocks[b].d = gguf_f32_to_f16(d);

    for(int i = 0; i < 16; i++) {
      int8_t v0 = (int8_t)roundf(in[i] * id);
      int8_t v1 = (int8_t)roundf(in[i + 16] * id);
      if(v0 < -8) v0 = -8; if(v0 > 7) v0 = 7;
      if(v1 < -8) v1 = -8; if(v1 > 7) v1 = 7;
      uint8_t q0 = (uint8_t)(v0 + 8);
      uint8_t q1 = (uint8_t)(v1 + 8);
      blocks[b].qs[i] = (uint8_t)(q0 | (q1 << 4));
    }
  }
  return 0;
}

// Quantize float array to Q8_0
TZ_CPU_EXPORT int tz_gguf_quantize_q8_0(const float* src, void* dst, int64_t k) {
  if(!src || !dst || k <= 0) return -1;
  block_q8_0* blocks = (block_q8_0*)dst;
  int64_t nb = k / 32;

  for(int64_t b = 0; b < nb; b++) {
    const float* in = src + b * 32;
    float max_val = 0.0f;
    for(int i = 0; i < 32; i++) {
      float abs_v = (float)fabs(in[i]);
      if(abs_v > max_val) max_val = abs_v;
    }
    float d = max_val / 127.0f;
    if(d == 0.0f) d = 1e-6f;
    float id = 1.0f / d;
    blocks[b].d = gguf_f32_to_f16(d);

    for(int i = 0; i < 32; i++) {
      int v = (int)roundf(in[i] * id);
      if(v < -128) v = -128; if(v > 127) v = 127;
      blocks[b].qs[i] = (int8_t)v;
    }
  }
  return 0;
}

// Matrix-Vector product: y = W * x directly with Q4_0 weights
TZ_CPU_EXPORT int tz_gguf_gemv_q4_0(const void* w, const float* x, float* y, int64_t rows, int64_t cols) {
  if(!w || !x || !y) return -1;
  const block_q4_0* blocks = (const block_q4_0*)w;
  int64_t blocks_per_row = cols / 32;

  int64_t r;
  #pragma omp parallel for schedule(static)
  for(r = 0; r < rows; r++) {
    float sum = 0.0f;
    const block_q4_0* row_blocks = blocks + r * blocks_per_row;
    for(int64_t b = 0; b < blocks_per_row; b++) {
      float d = gguf_f16_to_f32(row_blocks[b].d);
      const float* x_ptr = x + b * 32;
      float row_sum = 0.0f;
      for(int i = 0; i < 16; i++) {
        uint8_t q = row_blocks[b].qs[i];
        int8_t v0 = (int8_t)(q & 0x0F) - 8;
        int8_t v1 = (int8_t)(q >> 4)   - 8;
        row_sum += (float)v0 * x_ptr[i] + (float)v1 * x_ptr[i + 16];
      }
      sum += row_sum * d;
    }
    y[r] = sum;
  }
  return 0;
}

/* All offsets and lengths are checked against the physical file before any
 * allocation or seek. Supported tensor encodings match the reader below. */
#include <limits.h>

static int64_t gguf_tell(FILE* file) {
#ifdef _WIN32
  return _ftelli64(file);
#else
  return (int64_t)ftello(file);
#endif
}

static int gguf_seek(FILE* file, uint64_t offset) {
  if(offset > INT64_MAX) return 0;
#ifdef _WIN32
  return _fseeki64(file, (int64_t)offset, SEEK_SET) == 0;
#else
  return fseeko(file, (off_t)offset, SEEK_SET) == 0;
#endif
}

typedef struct { FILE* file; uint64_t position, end; } GgufReader;

static int gguf_read(GgufReader* r, void* dst, uint64_t count) {
  if(count > r->end - r->position || count > SIZE_MAX) return 0;
  if(count && fread(dst, 1, (size_t)count, r->file) != (size_t)count) return 0;
  r->position += count;
  return 1;
}

static int gguf_skip(GgufReader* r, uint64_t count) {
  if(count > r->end - r->position || !gguf_seek(r->file, r->position + count)) return 0;
  r->position += count;
  return 1;
}

static int gguf_u32(GgufReader* r, uint32_t* value) {
  unsigned char b[4];
  if(!gguf_read(r, b, 4)) return 0;
  *value = (uint32_t)b[0] | (uint32_t)b[1]<<8 | (uint32_t)b[2]<<16 | (uint32_t)b[3]<<24;
  return 1;
}

static int gguf_u64(GgufReader* r, uint64_t* value) {
  uint32_t lo, hi;
  if(!gguf_u32(r, &lo) || !gguf_u32(r, &hi)) return 0;
  *value = (uint64_t)lo | (uint64_t)hi<<32;
  return 1;
}

static int gguf_skip_value(GgufReader* r, uint32_t type, int depth) {
  static const unsigned char widths[13] = {1,1,2,2,4,4,4,1,0,0,8,8,8};
  if(type >= 13 || depth > 16) return 0;
  if(widths[type]) return gguf_skip(r, widths[type]);
  uint64_t count;
  if(type == 8) return gguf_u64(r, &count) && gguf_skip(r, count);
  uint32_t element;
  if(!gguf_u32(r, &element) || element >= 13 || !gguf_u64(r, &count)) return 0;
  if(widths[element]) {
    if(count > (r->end-r->position)/widths[element]) return 0;
    return gguf_skip(r, count*widths[element]);
  }
  if(count > (r->end-r->position)/8) return 0;
  for(uint64_t i=0;i<count;i++) if(!gguf_skip_value(r, element, depth+1)) return 0;
  return 1;
}

static int gguf_tensor_bytes(GgufTensorMeta* tensor) {
  uint64_t n=(uint64_t)tensor->numel, bytes;
  if(n > SIZE_MAX/sizeof(float)) return 0;
  switch(tensor->type) {
    case GGUF_TYPE_F32: bytes=n*4; break;
    case GGUF_TYPE_F16: bytes=n*2; break;
    case GGUF_TYPE_Q4_0:
    case GGUF_TYPE_Q8_0:
      if(tensor->shape[0]%32 || n%32) return 0;
      bytes=(n/32)*(tensor->type==GGUF_TYPE_Q4_0 ? sizeof(block_q4_0) : sizeof(block_q8_0));
      break;
    default: return 0;
  }
  tensor->size_bytes=bytes;
  return 1;
}

TZ_CPU_EXPORT int64_t tz_gguf_open(const char* path) {
  if(!path) return 0;
  FILE* file=fopen(path, "rb");
  if(!file) return 0;
#ifdef _WIN32
  int seek_end=_fseeki64(file, 0, SEEK_END);
#else
  int seek_end=fseeko(file, 0, SEEK_END);
#endif
  int64_t size=gguf_tell(file);
  if(seek_end || size<24 || !gguf_seek(file,0)) { fclose(file); return 0; }
  GgufReader reader={file,0,(uint64_t)size};
  GgufFile* gf=(GgufFile*)calloc(1,sizeof(GgufFile));
  if(!gf) { fclose(file); return 0; }
  gf->fp=file; gf->file_size=(uint64_t)size;
  uint32_t magic, alignment=32;
  int have_alignment=0;
  if(!gguf_u32(&reader,&magic) || magic!=GGUF_MAGIC ||
     !gguf_u32(&reader,&gf->version) || (gf->version!=2 && gf->version!=3) ||
     !gguf_u64(&reader,&gf->tensor_count) || !gguf_u64(&reader,&gf->kv_count) ||
     gf->tensor_count>MAX_GGUF_TENSORS || gf->kv_count>reader.end/13) goto invalid;

  for(uint64_t i=0;i<gf->kv_count;i++) {
    uint64_t length; uint32_t type; char key[17];
    if(!gguf_u64(&reader,&length) || !length) goto invalid;
    int is_alignment=0;
    if(length==sizeof(key)) {
      if(!gguf_read(&reader,key,length)) goto invalid;
      is_alignment=memcmp(key,"general.alignment",sizeof(key))==0;
    } else if(!gguf_skip(&reader,length)) goto invalid;
    if(!gguf_u32(&reader,&type)) goto invalid;
    if(is_alignment) {
      if(have_alignment || type!=4 || !gguf_u32(&reader,&alignment) ||
         !alignment || (alignment & (alignment-1))) goto invalid;
      have_alignment=1;
    } else if(!gguf_skip_value(&reader,type,0)) goto invalid;
  }

  if(gf->tensor_count) {
    gf->tensors=(GgufTensorMeta*)calloc((size_t)gf->tensor_count,sizeof(GgufTensorMeta));
    if(!gf->tensors) goto invalid;
  }
  gf->tensor_n=(int)gf->tensor_count;
  for(uint64_t i=0;i<gf->tensor_count;i++) {
    GgufTensorMeta* t=&gf->tensors[i];
    uint64_t length, elements=1;
    uint32_t dimensions;
    if(!gguf_u64(&reader,&length) || !length || length>=MAX_GGUF_NAME ||
       !gguf_read(&reader,t->name,length) || memchr(t->name,0,(size_t)length) ||
       !gguf_u32(&reader,&dimensions) || !dimensions || dimensions>MAX_GGUF_DIMS) goto invalid;
    t->name[length]=0; t->ndim=(int)dimensions;
    for(uint64_t previous=0;previous<i;previous++)
      if(strcmp(gf->tensors[previous].name,t->name)==0) goto invalid;
    for(uint32_t d=0;d<dimensions;d++) {
      uint64_t dimension;
      if(!gguf_u64(&reader,&dimension) || !dimension || dimension>INT64_MAX ||
         elements>INT64_MAX/dimension) goto invalid;
      t->shape[d]=(int64_t)dimension;
      elements*=dimension;
    }
    t->numel=(int64_t)elements;
    if(!gguf_u32(&reader,&t->type) || !gguf_u64(&reader,&t->offset) ||
       t->offset%alignment || !gguf_tensor_bytes(t)) goto invalid;
  }
  gf->data_base_offset=(reader.position+alignment-1)&~((uint64_t)alignment-1);
  if(gf->tensor_count && gf->data_base_offset>gf->file_size) goto invalid;
  for(uint64_t i=0;i<gf->tensor_count;i++) {
    GgufTensorMeta* t=&gf->tensors[i];
    uint64_t available=gf->file_size-gf->data_base_offset;
    if(t->offset>available || t->size_bytes>available-t->offset) goto invalid;
  }
  return (int64_t)(intptr_t)gf;
invalid:
  free(gf->tensors);
  fclose(file);
  free(gf);
  return 0;
}

TZ_CPU_EXPORT int64_t tz_gguf_num_tensors(int64_t handle) {
  GgufFile* gf = (GgufFile*)(intptr_t)handle;
  return gf ? (int64_t)gf->tensor_count : 0;
}

TZ_CPU_EXPORT const char* tz_gguf_tensor_name(int64_t handle, int64_t idx) {
  GgufFile* gf = (GgufFile*)(intptr_t)handle;
  if(!gf || idx < 0 || idx >= (int64_t)gf->tensor_count) return "";
  return gf->tensors[idx].name;
}

TZ_CPU_EXPORT int64_t tz_gguf_tensor_type(int64_t handle, int64_t idx) {
  GgufFile* gf = (GgufFile*)(intptr_t)handle;
  if(!gf || idx < 0 || idx >= (int64_t)gf->tensor_count) return -1;
  return (int64_t)gf->tensors[idx].type;
}

TZ_CPU_EXPORT int64_t tz_gguf_tensor_shape(int64_t handle, int64_t idx, int64_t dim_idx) {
  GgufFile* gf = (GgufFile*)(intptr_t)handle;
  if(!gf || idx < 0 || idx >= (int64_t)gf->tensor_count || dim_idx < 0 || dim_idx >= gf->tensors[idx].ndim) return 1;
  return gf->tensors[idx].shape[dim_idx];
}

TZ_CPU_EXPORT int64_t tz_gguf_tensor_numel(int64_t handle, int64_t idx) {
  GgufFile* gf = (GgufFile*)(intptr_t)handle;
  if(!gf || idx < 0 || idx >= (int64_t)gf->tensor_count) return 0;
  return gf->tensors[idx].numel;
}

TZ_CPU_EXPORT int tz_gguf_read_tensor_f32(int64_t handle, int64_t idx, float* dst) {
  GgufFile* gf=(GgufFile*)(intptr_t)handle;
  if(!gf || idx<0 || idx>=(int64_t)gf->tensor_count || !dst) return -1;
  GgufTensorMeta* t=&gf->tensors[idx];
  if(!gguf_seek(gf->fp,gf->data_base_offset+t->offset)) return -1;
  if(t->type==GGUF_TYPE_F32)
    return fread(dst,sizeof(float),(size_t)t->numel,gf->fp)==(size_t)t->numel ? 0 : -1;
  void* buffer=malloc((size_t)t->size_bytes);
  if(!buffer) return -1;
  if(fread(buffer,1,(size_t)t->size_bytes,gf->fp)!=(size_t)t->size_bytes) {
    free(buffer);
    return -1;
  }
  int result=0;
  if(t->type==GGUF_TYPE_F16) {
    uint16_t* half=(uint16_t*)buffer;
    for(int64_t i=0;i<t->numel;i++) dst[i]=gguf_f16_to_f32(half[i]);
  } else if(t->type==GGUF_TYPE_Q4_0) {
    result=tz_gguf_dequantize_q4_0(buffer,dst,t->numel);
  } else if(t->type==GGUF_TYPE_Q8_0) {
    result=tz_gguf_dequantize_q8_0(buffer,dst,t->numel);
  } else result=-2;
  free(buffer);
  return result;
}

TZ_CPU_EXPORT int tz_gguf_close(int64_t handle) {
  GgufFile* gf = (GgufFile*)(intptr_t)handle;
  if(!gf) return -1;
  if(gf->fp) fclose(gf->fp);
  if(gf->tensors) free(gf->tensors);
  free(gf);
  return 0;
}
