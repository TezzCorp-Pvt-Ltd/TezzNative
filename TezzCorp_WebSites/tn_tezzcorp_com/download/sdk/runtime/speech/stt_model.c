#include "speech.h"
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/* Explicit CPU baseline. Requested CUDA execution is not silently substituted.
 * Handles contain a generation, preventing use-after-free on slot reuse.
 * Concurrent calls on one model fail with -1; callers may retry. */
typedef struct {
  SpeechNet net;
  SpeechLanguage* language;
  int context, width, lm_weight_ppm, insertion_ppm;
  atomic_flag busy;
} SttModel;
typedef struct { SttModel* model; uint32_t generation; } SttSlot;
static SttSlot slots[16];
static atomic_flag registry_busy = ATOMIC_FLAG_INIT;

static SttModel* acquire(long long handle){
  if(handle <= 0 || atomic_flag_test_and_set(&registry_busy)) return NULL;
  uint64_t id = (uint64_t)handle;
  unsigned index = (unsigned)(id & 255u);
  SttModel* model = NULL;
  if(index >= 1 && index <= 16 && (id >> 8) == slots[index - 1].generation){
    SttModel* candidate = slots[index - 1].model;
    if(candidate && !atomic_flag_test_and_set(&candidate->busy)) model = candidate;
  }
  atomic_flag_clear(&registry_busy);
  return model;
}

static long long register_model(SttModel* model){
  if(atomic_flag_test_and_set(&registry_busy)) return 0;
  long long handle = 0;
  for(unsigned i = 0; i < 16; ++i){
    if(slots[i].model) continue;
    if(++slots[i].generation == 0) ++slots[i].generation;
    slots[i].model = model;
    handle = (long long)(((uint64_t)slots[i].generation << 8) | (i + 1));
    break;
  }
  atomic_flag_clear(&registry_busy);
  return handle;
}

static void destroy(SttModel* model){ speech_net_free(&model->net); speech_language_free(model->language); free(model); }

TN_NATIVE_API long long tn_speech_stt_create(long long hidden, long long context, long long seed){
  if(hidden < 8 || hidden > 256 || context < 0 || context > 10) return 0;
  SttModel* model = (SttModel*)calloc(1, sizeof(SttModel));
  if(!model) return 0;
  atomic_flag_clear(&model->busy);
  model->context = (int)context;
  model->width = 16;
  if(speech_net_init(&model->net, TN_SPEECH_MELS * (2 * (int)context + 1), (int)hidden, TN_SPEECH_CLASSES, (uint32_t)seed)){
    free(model); return 0;
  }
  long long handle = register_model(model);
  if(!handle) destroy(model);
  return handle;
}

TN_NATIVE_API long long tn_speech_stt_free(long long handle){
  SttModel* model = acquire(handle);
  if(!model) return -1;
  if(atomic_flag_test_and_set(&registry_busy)){ atomic_flag_clear(&model->busy); return -1; }
  slots[((uint64_t)handle & 255u) - 1].model = NULL;
  atomic_flag_clear(&registry_busy);
  destroy(model);
  return 0;
}

static int forward(SttModel* model, const double* mel, int frames, float** input, float** hidden, float** logits){
  *input = NULL; *hidden = NULL; *logits = NULL;
  if(!mel || frames < 1 || frames > TN_SPEECH_MAX_FRAMES) return -1;
  *input = (float*)malloc((size_t)frames * model->net.inputs * sizeof(float));
  *hidden = (float*)malloc((size_t)frames * model->net.hidden * sizeof(float));
  *logits = (float*)malloc((size_t)frames * model->net.outputs * sizeof(float));
  if(!*input || !*hidden || !*logits) return -1;
  for(int t = 0; t < frames; ++t){
    int offset = 0;
    for(int delta = -model->context; delta <= model->context; ++delta){
      int source = t + delta;
      if(source < 0) source = 0;
      if(source >= frames) source = frames - 1;
      for(int m = 0; m < TN_SPEECH_MELS; ++m){
        double value = mel[(size_t)source * TN_SPEECH_MELS + m];
        if(!isfinite(value)) return -1;
        value = (value + 10.0) * 0.1;
        if(value > 5.0) value = 5.0;
        if(value < -5.0) value = -5.0;
        (*input)[(size_t)t * model->net.inputs + offset++] = (float)value;
      }
    }
  }
  return speech_net_forward(&model->net, *input, frames, *hidden, *logits);
}

static long long objective(long long handle, const double* mel, long long frames,
  const int64_t* labels, long long count, long long rate_ppm, double* loss, int64_t* alignment){
  if(!loss || frames < 1 || frames > TN_SPEECH_MAX_FRAMES || count < 1 || count > TN_SPEECH_MAX_LABELS ||
      !labels || rate_ppm < 0 || rate_ppm > 100000) return -1;
  SttModel* model = acquire(handle);
  if(!model) return -1;
  float *input = NULL, *hidden = NULL, *logits = NULL, *gradient = NULL;
  int* path = NULL;
  long long status = -1;
  if(forward(model, mel, (int)frames, &input, &hidden, &logits)) goto done;
  if(rate_ppm){
    gradient = (float*)malloc((size_t)frames * TN_SPEECH_CLASSES * sizeof(float));
    if(!gradient) goto done;
  }
  if(alignment){ path = (int*)malloc((size_t)frames * sizeof(int)); if(!path) goto done; }
  if(speech_ctc(logits, (int)frames, TN_SPEECH_CLASSES, labels, (int)count, gradient, path, loss)) goto done;
  *loss /= (double)count;
  if(gradient){
    for(size_t i = 0; i < (size_t)frames * TN_SPEECH_CLASSES; ++i) gradient[i] /= (float)count;
    if(speech_net_update(&model->net, input, hidden, gradient, (int)frames, (float)rate_ppm * 1.0e-6f)) goto done;
  }
  if(alignment) for(int t = 0; t < frames; ++t) alignment[t] = path[t];
  status = 0;
done:
  free(input); free(hidden); free(logits); free(gradient); free(path);
  atomic_flag_clear(&model->busy);
  return status;
}

TN_NATIVE_API long long tn_speech_stt_train(long long handle, const double* mel, long long frames,
  const int64_t* labels, long long count, long long rate_ppm, double* loss){
  if(rate_ppm <= 0) return -1;
  return objective(handle, mel, frames, labels, count, rate_ppm, loss, NULL);
}

TN_NATIVE_API long long tn_speech_stt_loss(long long handle, const double* mel, long long frames,
  const int64_t* labels, long long count, double* loss){
  return objective(handle, mel, frames, labels, count, 0, loss, NULL);
}

TN_NATIVE_API long long tn_speech_stt_align(long long handle, const double* mel, long long frames,
  const int64_t* labels, long long count, int64_t* alignment, double* loss){
  if(!alignment) return -1;
  return objective(handle, mel, frames, labels, count, 0, loss, alignment);
}

TN_NATIVE_API long long tn_speech_stt_predict(long long handle, const double* mel, long long frames,
  char* text, long long capacity){
  if(!text || capacity < 1 || capacity > 65536 || frames < 1 || frames > TN_SPEECH_MAX_FRAMES) return -1;
  text[0] = 0;
  SttModel* model = acquire(handle);
  if(!model) return -1;
  float *input = NULL, *hidden = NULL, *logits = NULL;
  int* decoded = NULL;
  long long length = -1;
  if(!model->net.step || forward(model, mel, (int)frames, &input, &hidden, &logits)) goto done;
  decoded = malloc((size_t)frames * sizeof(int));
  if(!decoded) goto done;
  int count = speech_ctc_decode_language(logits, (int)frames, TN_SPEECH_CLASSES, model->width,
    model->language, model->lm_weight_ppm * 1.0e-6, model->insertion_ppm * 1.0e-6, decoded, (int)frames, NULL);
  if(count < 0) goto done;
  length = 0;
  for(int t = 0; t < count; ++t){
    int best = decoded[t];
    char value = best == 1 ? ' ' : best == 2 ? '\'' : (char)('a' + best - 3);
    if(value != ' ' || (length && text[length - 1] != ' ')){
      if(length + 1 >= capacity){ length = -1; text[0] = 0; goto done; }
      text[length++] = value;
    }
  }
  if(length && text[length - 1] == ' ') length--;
  text[length] = 0;
done:
  free(input); free(hidden); free(logits); free(decoded);
  atomic_flag_clear(&model->busy);
  return length;
}

TN_NATIVE_API long long tn_speech_stt_steps(long long handle){
  SttModel* model = acquire(handle);
  if(!model) return -1;
  long long step = (long long)model->net.step;
  atomic_flag_clear(&model->busy);
  return step;
}

TN_NATIVE_API long long tn_speech_stt_text_reset(long long handle){
  SttModel* model = acquire(handle);
  if(!model) return -1;
  speech_language_free(model->language);
  model->language = NULL;
  model->lm_weight_ppm = model->insertion_ppm = 0;
  atomic_flag_clear(&model->busy);
  return 0;
}

TN_NATIVE_API long long tn_speech_stt_text_train(long long handle, const int64_t* labels, long long count){
  if(!labels || count < 1 || count > TN_SPEECH_MAX_LABELS) return -1;
  SttModel* model = acquire(handle);
  if(!model) return -1;
  if(!model->language) model->language = speech_language_create();
  long long status = speech_language_train(model->language, labels, (int)count);
  atomic_flag_clear(&model->busy);
  return status;
}

TN_NATIVE_API long long tn_speech_stt_decoder(long long handle, long long width, long long weight, long long insertion){
  if(width < 1 || width > 128 || weight < 0 || weight > 2000000 || insertion < 0 || insertion > 5000000) return -1;
  SttModel* model = acquire(handle);
  if(!model) return -1;
  long long status = -1;
  if(!weight || speech_language_tokens(model->language)){
    model->width = (int)width;
    model->lm_weight_ppm = (int)weight;
    model->insertion_ppm = (int)insertion;
    status = 0;
  }
  atomic_flag_clear(&model->busy);
  return status;
}

static uint32_t decoder_crc(const uint32_t* settings){
  uint32_t crc = ~0u;
  for(int i = 0; i < 4; ++i){
    for(unsigned shift = 0; shift < 32; shift += 8){
      crc ^= (settings[i] >> shift) & 255u;
      for(int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

TN_NATIVE_API long long tn_speech_stt_save(long long handle, const char* path){
  if(!path || !*path || strlen(path) > 4096) return -1;
  SttModel* model = acquire(handle);
  if(!model) return -1;
  long long status = -1;
  FILE* file = NULL;
  if(!model->net.step) goto done;
  file = fopen(path, "wb");
  if(!file) goto done;
  const unsigned char magic[8] = {'T','N','S','T','T','0','2',0};
  uint32_t config[9] = {2,0x01020304,16000,400,512,160,TN_SPEECH_MELS,(uint32_t)model->context,(uint32_t)model->net.hidden};
  uint32_t decoder[5] = {(uint32_t)model->width, (uint32_t)model->lm_weight_ppm,
                        (uint32_t)model->insertion_ppm, speech_language_tokens(model->language) ? 1u : 0u, 0};
  decoder[4] = decoder_crc(decoder);
  if(fwrite(magic,sizeof(magic),1,file)==1 && fwrite(config,sizeof(config),1,file)==1 &&
      !speech_net_write(file,&model->net) && fwrite(decoder,sizeof(decoder),1,file)==1 &&
      (!decoder[3] || !speech_language_write(file,model->language))) status=0;
  if(fclose(file)) status=-1;
done:
  atomic_flag_clear(&model->busy);
  return status;
}

TN_NATIVE_API long long tn_speech_stt_load(const char* path){
  if(!path || !*path || strlen(path) > 4096) return 0;
  FILE* file = fopen(path, "rb");
  if(!file) return 0;
  unsigned char magic[8]; uint32_t config[9];
  const unsigned char expected[8] = {'T','N','S','T','T','0','1',0};
  SttModel* model = NULL;
  long long handle = 0;
  if(fread(magic,sizeof(magic),1,file)!=1 || memcmp(magic,expected,6) || magic[7] ||
      (magic[6]!='1' && magic[6]!='2') || fread(config,sizeof(config),1,file)!=1) goto done;
  if(config[0]!=(uint32_t)(magic[6]-'0') || config[1]!=0x01020304 || config[2]!=16000 || config[3]!=400 || config[4]!=512 || config[5]!=160 ||
      config[6]!=TN_SPEECH_MELS || config[7]>10 || config[8]<8 || config[8]>256) goto done;
  model=(SttModel*)calloc(1,sizeof(SttModel));
  if(!model) goto done;
  atomic_flag_clear(&model->busy);
  model->context=(int)config[7];
  model->width=16;
  if(speech_net_read(file,&model->net,TN_SPEECH_MELS*(2*model->context+1),(int)config[8],TN_SPEECH_CLASSES) || !model->net.step) goto done;
  if(config[0]==2){
    uint32_t decoder[5];
    if(fread(decoder,sizeof(decoder),1,file)!=1 || decoder[0]<1 || decoder[0]>128 ||
       decoder[1]>2000000 || decoder[2]>5000000 || decoder[3]>1 || decoder[4]!=decoder_crc(decoder)) goto done;
    model->width=(int)decoder[0]; model->lm_weight_ppm=(int)decoder[1]; model->insertion_ppm=(int)decoder[2];
    if(decoder[3]){ model->language=speech_language_read(file); if(!model->language) goto done; }
    if(model->lm_weight_ppm && !model->language) goto done;
  }
  if(fgetc(file)!=EOF || ferror(file)) goto done;
  handle=register_model(model);
  if(handle) model=NULL;
done:
  if(model) destroy(model);
  fclose(file);
  return handle;
}
