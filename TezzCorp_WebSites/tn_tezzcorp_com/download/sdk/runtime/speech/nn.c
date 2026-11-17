#include "speech.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int net_dimensions(int inputs, int hidden, int outputs, size_t* count){
  if(inputs < 1 || inputs > 4096 || hidden < 1 || hidden > 512 || outputs < 1 || outputs > 512) return -1;
  *count = (size_t)inputs * hidden + hidden + (size_t)hidden * outputs + outputs;
  return *count <= 3000000 ? 0 : -1;
}

int speech_net_init(SpeechNet* net, int inputs, int hidden, int outputs, uint32_t seed){
  size_t count;
  if(!net || net_dimensions(inputs, hidden, outputs, &count)) return -1;
  memset(net, 0, sizeof(*net));
  net->inputs = inputs; net->hidden = hidden; net->outputs = outputs; net->count = count;
  net->weights = (float*)calloc(count, sizeof(float));
  net->moment = (float*)calloc(count, sizeof(float));
  net->variance = (float*)calloc(count, sizeof(float));
  if(!net->weights || !net->moment || !net->variance){ speech_net_free(net); return -1; }
  uint32_t state = seed;
  size_t w2 = (size_t)inputs * hidden + hidden;
  for(size_t i = 0; i < count; ++i){
    if((i >= (size_t)inputs * hidden && i < w2) || i >= w2 + (size_t)hidden * outputs) continue;
    state = state * 1664525u + 1013904223u; /* deterministic weight initialization */
    float unit = (float)(state >> 8) / 16777216.0f;
    net->weights[i] = (unit * 2.0f - 1.0f) * sqrtf(6.0f / (i < w2 ? (float)inputs : (float)hidden));
  }
  return 0;
}

void speech_net_free(SpeechNet* net){
  if(!net) return;
  free(net->weights); free(net->moment); free(net->variance);
  memset(net, 0, sizeof(*net));
}

int speech_net_forward(const SpeechNet* net, const float* input, int rows, float* hidden, float* output){
  if(!net || !net->weights || !input || !hidden || !output || rows < 1 || rows > TN_SPEECH_MAX_FRAMES) return -1;
  int ni = net->inputs, nh = net->hidden, no = net->outputs;
  const float* b1 = net->weights + (size_t)ni * nh;
  const float* w2 = b1 + nh;
  const float* b2 = w2 + (size_t)nh * no;
  for(int r = 0; r < rows; ++r){
    const float* x = input + (size_t)r * ni;
    float* h = hidden + (size_t)r * nh;
    float* y = output + (size_t)r * no;
    for(int j = 0; j < nh; ++j){
      float sum = b1[j];
      const float* w = net->weights + (size_t)j * ni;
      for(int k = 0; k < ni; ++k) sum += w[k] * x[k];
      if(!isfinite(sum)) return -1;
      h[j] = sum > 0.0f ? sum : 0.0f;
    }
    for(int j = 0; j < no; ++j){
      float sum = b2[j];
      const float* w = w2 + (size_t)j * nh;
      for(int k = 0; k < nh; ++k) sum += w[k] * h[k];
      if(!isfinite(sum)) return -1;
      y[j] = sum;
    }
  }
  return 0;
}

int speech_net_update(SpeechNet* net, const float* input, const float* hidden,
                      const float* output_gradient, int rows, float learning_rate){
  if(!net || !input || !hidden || !output_gradient || rows < 1 || rows > TN_SPEECH_MAX_FRAMES ||
      !isfinite(learning_rate) || learning_rate <= 0.0f || learning_rate > 0.1f) return -1;
  int ni = net->inputs, nh = net->hidden, no = net->outputs;
  size_t b1 = (size_t)ni * nh, w2 = b1 + nh, b2 = w2 + (size_t)nh * no;
  float* gradient = (float*)calloc(net->count, sizeof(float));
  float* dh = (float*)calloc((size_t)nh, sizeof(float));
  if(!gradient || !dh){ free(gradient); free(dh); return -1; }
  for(int r = 0; r < rows; ++r){
    const float* x = input + (size_t)r * ni;
    const float* h = hidden + (size_t)r * nh;
    const float* dy = output_gradient + (size_t)r * no;
    memset(dh, 0, (size_t)nh * sizeof(float));
    for(int j = 0; j < no; ++j){
      if(!isfinite(dy[j])){ free(gradient); free(dh); return -1; }
      gradient[b2 + j] += dy[j];
      for(int k = 0; k < nh; ++k){
        gradient[w2 + (size_t)j * nh + k] += dy[j] * h[k];
        dh[k] += dy[j] * net->weights[w2 + (size_t)j * nh + k];
      }
    }
    for(int j = 0; j < nh; ++j){
      if(h[j] <= 0.0f) continue;
      gradient[b1 + j] += dh[j];
      for(int k = 0; k < ni; ++k) gradient[(size_t)j * ni + k] += dh[j] * x[k];
    }
  }
  double norm = 0.0;
  for(size_t i = 0; i < net->count; ++i){
    if(!isfinite(gradient[i])){ free(gradient); free(dh); return -1; }
    norm += (double)gradient[i] * gradient[i];
  }
  norm = sqrt(norm);
  float scale = norm > 5.0 ? (float)(5.0 / norm) : 1.0f;
  uint64_t next = net->step + 1;
  double correction1 = 1.0 - pow(0.9, (double)next);
  double correction2 = 1.0 - pow(0.999, (double)next);
  for(size_t i = 0; i < net->count; ++i){
    float g = gradient[i] * scale;
    net->moment[i] = 0.9f * net->moment[i] + 0.1f * g;
    net->variance[i] = 0.999f * net->variance[i] + 0.001f * g * g;
    net->weights[i] -= (float)(learning_rate * (net->moment[i] / correction1) /
                              (sqrt(net->variance[i] / correction2) + 1.0e-8));
  }
  net->step = next;
  free(gradient); free(dh);
  return 0;
}

static uint32_t crc_update(uint32_t crc, const void* bytes, size_t size){
  const unsigned char* p = (const unsigned char*)bytes;
  for(size_t i = 0; i < size; ++i){
    crc ^= p[i];
    for(int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return crc;
}

int speech_net_write(FILE* file, const SpeechNet* net){
  uint32_t dims[4] = {(uint32_t)net->inputs, (uint32_t)net->hidden, (uint32_t)net->outputs, (uint32_t)net->count};
  uint32_t crc = crc_update(~0u, dims, sizeof(dims));
  crc = crc_update(crc, &net->step, sizeof(net->step));
  if(fwrite(dims, sizeof(dims), 1, file) != 1 || fwrite(&net->step, sizeof(net->step), 1, file) != 1) return -1;
  const float* blocks[3] = {net->weights, net->moment, net->variance};
  for(int j = 0; j < 3; ++j){
    if(fwrite(blocks[j], sizeof(float), net->count, file) != net->count) return -1;
    crc = crc_update(crc, blocks[j], net->count * sizeof(float));
  }
  crc = ~crc;
  return fwrite(&crc, sizeof(crc), 1, file) == 1 ? 0 : -1;
}

int speech_net_read(FILE* file, SpeechNet* net, int inputs, int hidden, int outputs){
  uint32_t dims[4]; uint64_t step; size_t count;
  if(fread(dims, sizeof(dims), 1, file) != 1 || fread(&step, sizeof(step), 1, file) != 1 ||
      net_dimensions(inputs, hidden, outputs, &count) || dims[0] != (uint32_t)inputs ||
      dims[1] != (uint32_t)hidden || dims[2] != (uint32_t)outputs || dims[3] != count || step > 1000000000u) return -1;
  if(speech_net_init(net, inputs, hidden, outputs, 0)) return -1;
  uint32_t crc = crc_update(~0u, dims, sizeof(dims));
  crc = crc_update(crc, &step, sizeof(step));
  float* blocks[3] = {net->weights, net->moment, net->variance};
  for(int j = 0; j < 3; ++j){
    if(fread(blocks[j], sizeof(float), count, file) != count) goto invalid;
    crc = crc_update(crc, blocks[j], count * sizeof(float));
    for(size_t i = 0; i < count; ++i){
      float value = blocks[j][i];
      if(!isfinite(value) || (j < 2 && fabsf(value) > 1.0e6f) || (j == 2 && (value < 0 || value > 1.0e12f))) goto invalid;
    }
  }
  uint32_t stored;
  if(fread(&stored, sizeof(stored), 1, file) != 1 || stored != ~crc) goto invalid;
  net->step = step;
  return 0;
invalid:
  speech_net_free(net);
  return -1;
}
