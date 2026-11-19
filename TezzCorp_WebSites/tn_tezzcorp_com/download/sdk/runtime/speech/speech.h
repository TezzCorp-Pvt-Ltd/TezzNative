#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "native_abi.h"

#define TN_SPEECH_MELS 80
#define TN_SPEECH_CLASSES 29
#define TN_SPEECH_MAX_FRAMES 3000
#define TN_SPEECH_MAX_LABELS 512

/* Float32 parameters, float64 CTC accumulators. No external ML dependency. */
typedef struct {
  int inputs, hidden, outputs;
  size_t count;
  uint64_t step;
  float *weights, *moment, *variance;
} SpeechNet;

int speech_net_init(SpeechNet* net, int inputs, int hidden, int outputs, uint32_t seed);
void speech_net_free(SpeechNet* net);
int speech_net_forward(const SpeechNet* net, const float* input, int rows, float* hidden, float* output);
int speech_net_update(SpeechNet* net, const float* input, const float* hidden, const float* output_gradient, int rows, float learning_rate);
int speech_net_write(FILE* file, const SpeechNet* net);
int speech_net_read(FILE* file, SpeechNet* net, int inputs, int hidden, int outputs);

/* Gradient is d(-log P(labels|logits))/d(logits), without length reduction.
 * Optional alignment contains a target-label INDEX per frame, not a symbol.
 * Blank frames are assigned to the preceding character (leading blank to 0).
 * An impossible alignment returns -1 without producing fabricated labels. */
int speech_ctc(const float* logits, int frames, int classes, const int64_t* labels,
               int label_count, float* gradient, int* alignment, double* loss);

/* Bounded prefix beam search; output excludes blank and merges repeat paths.
 * Returns label count or -1. Output remains untouched on error. The optional
 * log probability sums retained alignments, and is approximate when pruned. */
int speech_ctc_decode(const float* logits, int frames, int classes, int width,
                      int* labels, int capacity, double* log_probability);

/* Generic host CTC primitives; logits/gradient are f32 buffers, labels i64. */
TN_NATIVE_API unsigned char* tn_speech_file_open(const char* path, long long exclusive);
TN_NATIVE_API long long tn_speech_file_replace(const char* source, const char* target);
TN_NATIVE_API long long tn_speech_file_remove(const char* path);
TN_NATIVE_API long long tn_speech_ctc_loss_f32(long long logits, long long frames,
  long long classes, const int64_t* labels, long long count, long long gradient, double* loss);
TN_NATIVE_API long long tn_speech_ctc_decode_f32(long long logits, long long frames,
  long long classes, long long width, int64_t* labels, long long capacity);

typedef struct SpeechLanguage SpeechLanguage;
SpeechLanguage* speech_language_create(void);
void speech_language_free(SpeechLanguage* model);
uint64_t speech_language_tokens(const SpeechLanguage* model);
int speech_language_train(SpeechLanguage* model, const int64_t* labels, int length);
double speech_language_logp(const SpeechLanguage* model, const uint16_t* prefix, int length, int label);
int speech_language_write(FILE* file, const SpeechLanguage* model);
SpeechLanguage* speech_language_read(FILE* file);
/* Return acoustic log probability; ranking additionally uses explicit LM and
 * per-character insertion scores. A zero LM weight preserves the old decoder. */
int speech_ctc_decode_language(const float* logits, int frames, int classes, int width,
  const SpeechLanguage* language, double weight, double insertion,
  int* labels, int capacity, double* log_probability);

TN_NATIVE_API long long tn_speech_stt_create(long long hidden, long long context, long long seed);
TN_NATIVE_API long long tn_speech_stt_free(long long handle);
TN_NATIVE_API long long tn_speech_stt_train(long long handle, const double* mel, long long frames,
  const int64_t* labels, long long label_count, long long learning_rate_ppm, double* loss);
TN_NATIVE_API long long tn_speech_stt_loss(long long handle, const double* mel, long long frames,
  const int64_t* labels, long long label_count, double* loss);
TN_NATIVE_API long long tn_speech_stt_predict(long long handle, const double* mel, long long frames,
  char* text, long long capacity);
TN_NATIVE_API long long tn_speech_stt_align(long long handle, const double* mel, long long frames,
  const int64_t* labels, long long label_count, int64_t* alignment, double* loss);
TN_NATIVE_API long long tn_speech_stt_save(long long handle, const char* path);
TN_NATIVE_API long long tn_speech_stt_load(const char* path);
TN_NATIVE_API long long tn_speech_stt_steps(long long handle);
TN_NATIVE_API long long tn_speech_stt_text_reset(long long handle);
TN_NATIVE_API long long tn_speech_stt_text_train(long long handle, const int64_t* labels, long long count);
TN_NATIVE_API long long tn_speech_stt_decoder(long long handle, long long width, long long lm_weight_ppm, long long insertion_ppm);

int speech_vocode(const double* mel, int frames, int64_t* pcm, int capacity, double pitch, double energy);
int speech_neural_vocoder_load(const char* weights_path);
void speech_neural_vocoder_free(void);
int speech_neural_vocoder_is_loaded(void);
int speech_neural_vocode(const double* mel, int frames, int64_t* pcm, int capacity, double pitch, double energy);
TN_NATIVE_API long long tn_speech_tts_load_vocoder(const char* path);
TN_NATIVE_API long long tn_speech_tts_create(long long hidden, long long seed);
TN_NATIVE_API long long tn_speech_tts_free(long long handle);
TN_NATIVE_API long long tn_speech_tts_train(long long handle, const double* mel, long long frames,
  const int64_t* labels, long long count, const int64_t* alignment, long long rate_ppm, double* loss);
TN_NATIVE_API long long tn_speech_tts_loss(long long handle, const double* mel, long long frames,
  const int64_t* labels, long long count, const int64_t* alignment, double* loss);
TN_NATIVE_API long long tn_speech_tts_mel(long long handle, const int64_t* labels, long long count,
  double* mel, long long capacity_frames, long long speed_ppm);
TN_NATIVE_API long long tn_speech_tts_pcm(long long handle, const int64_t* labels, long long count,
  int64_t* pcm, long long capacity, long long speed_ppm, long long pitch_ppm, long long energy_ppm);
TN_NATIVE_API long long tn_speech_tts_save(long long handle, const char* path);
TN_NATIVE_API long long tn_speech_tts_load(const char* path);
TN_NATIVE_API long long tn_speech_tts_steps(long long handle);
