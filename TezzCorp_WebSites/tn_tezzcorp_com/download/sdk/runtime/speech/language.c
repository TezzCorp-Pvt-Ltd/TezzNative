/* Native character 4-gram model. Stores aggregate counts, never transcripts. */
#include "speech.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define LM_CONTEXTS (1 + 29 + 29 * 29 + 29 * 29 * 29)
#define LM_COUNTS (LM_CONTEXTS * TN_SPEECH_CLASSES)
#define LM_MAX_TOKENS UINT64_C(1000000000)
struct SpeechLanguage { uint64_t tokens; uint32_t counts[LM_COUNTS]; };

SpeechLanguage* speech_language_create(void){ return calloc(1, sizeof(SpeechLanguage)); }
void speech_language_free(SpeechLanguage* model){ free(model); }
uint64_t speech_language_tokens(const SpeechLanguage* model){ return model ? model->tokens : 0; }

static unsigned row_offset(const uint16_t* prefix, int length, int order){
  unsigned offset = 0, power = 1, key = 0;
  for(int n = 0; n < order; ++n){ offset += power; power *= TN_SPEECH_CLASSES; }
  for(int i = length - order; i < length; ++i)
    key = key * TN_SPEECH_CLASSES + (i >= 0 ? prefix[i] : 0);
  return (offset + key) * TN_SPEECH_CLASSES;
}

int speech_language_train(SpeechLanguage* model, const int64_t* labels, int length){
  if(!model || !labels || length < 1 || length > TN_SPEECH_MAX_LABELS ||
     model->tokens > LM_MAX_TOKENS - (uint64_t)length) return -1;
  uint16_t text[TN_SPEECH_MAX_LABELS];
  for(int i = 0; i < length; ++i){
    if(labels[i] < 1 || labels[i] >= TN_SPEECH_CLASSES) return -1;
    text[i] = (uint16_t)labels[i];
  }
  for(int i = 0; i < length; ++i){
    for(int order = 0; order <= 3; ++order){
      uint32_t* row = model->counts + row_offset(text, i, order);
      row[0]++; row[text[i]]++;
    }
  }
  model->tokens += (uint64_t)length;
  return 0;
}

double speech_language_logp(const SpeechLanguage* model, const uint16_t* prefix, int length, int label){
  if(!model || !model->tokens || length < 0 || length > TN_SPEECH_MAX_FRAMES ||
     (length && !prefix) || label < 1 || label >= TN_SPEECH_CLASSES) return -INFINITY;
  /* Only the final three symbols are needed; older text is never inspected. */
  for(int i = length > 3 ? length - 3 : 0; i < length; ++i)
    if(prefix[i] < 1 || prefix[i] >= TN_SPEECH_CLASSES) return -INFINITY;
  double probability = 1.0 / (TN_SPEECH_CLASSES - 1);
  for(int order = 0; order <= 3; ++order){
    const uint32_t* row = model->counts + row_offset(prefix, length, order);
    /* A normalized interpolated Dirichlet prior, including unseen contexts. */
    probability = (row[label] + 10.0 * probability) / (row[0] + 10.0);
  }
  return log(probability);
}

static uint32_t crc_bytes(uint32_t crc, const void* data, size_t bytes){
  const unsigned char* p = data;
  for(size_t i = 0; i < bytes; ++i){
    crc ^= p[i];
    for(int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return crc;
}

int speech_language_write(FILE* file, const SpeechLanguage* model){
  if(!file || !model || !model->tokens || model->tokens > LM_MAX_TOKENS) return -1;
  uint32_t count = LM_COUNTS;
  uint32_t crc = crc_bytes(~0u, &count, sizeof(count));
  crc = crc_bytes(crc, &model->tokens, sizeof(model->tokens));
  crc = ~crc_bytes(crc, model->counts, sizeof(model->counts));
  return fwrite(&count, sizeof(count), 1, file) == 1 &&
         fwrite(&model->tokens, sizeof(model->tokens), 1, file) == 1 &&
         fwrite(model->counts, sizeof(model->counts), 1, file) == 1 &&
         fwrite(&crc, sizeof(crc), 1, file) == 1 ? 0 : -1;
}

SpeechLanguage* speech_language_read(FILE* file){
  if(!file) return NULL;
  uint32_t count, stored;
  SpeechLanguage* model = speech_language_create();
  if(!model) return NULL;
  if(fread(&count, sizeof(count), 1, file) != 1 || count != LM_COUNTS ||
     fread(&model->tokens, sizeof(model->tokens), 1, file) != 1 || !model->tokens || model->tokens > LM_MAX_TOKENS ||
     fread(model->counts, sizeof(model->counts), 1, file) != 1 || fread(&stored, sizeof(stored), 1, file) != 1) goto invalid;
  uint32_t crc = crc_bytes(~0u, &count, sizeof(count));
  crc = crc_bytes(crc, &model->tokens, sizeof(model->tokens));
  if(~crc_bytes(crc, model->counts, sizeof(model->counts)) != stored) goto invalid;
  if(model->counts[0] != model->tokens) goto invalid;
  for(unsigned i = 0; i < LM_COUNTS; i += TN_SPEECH_CLASSES){
    uint64_t total = 0;
    for(int c = 1; c < TN_SPEECH_CLASSES; ++c) total += model->counts[i+c];
    if(total != model->counts[i] || total > model->tokens) goto invalid;
  }
  return model;
invalid:
  speech_language_free(model);
  return NULL;
}
