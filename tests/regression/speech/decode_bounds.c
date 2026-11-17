#include "speech.h"
#include <math.h>
#include <stdlib.h>

int main(void){
  float* logits = malloc((size_t)TN_SPEECH_MAX_FRAMES * 2 * sizeof(float));
  int* labels = malloc((size_t)(TN_SPEECH_MAX_FRAMES + 2) * sizeof(int));
  if(!logits || !labels) return 1;
  for(int t = 0; t < TN_SPEECH_MAX_FRAMES; ++t){
    logits[t * 2] = t & 1 ? 20.0f : -20.0f;
    logits[t * 2 + 1] = -logits[t * 2];
  }
  labels[0] = labels[TN_SPEECH_MAX_FRAMES + 1] = 917;
  int count = speech_ctc_decode(logits, TN_SPEECH_MAX_FRAMES, 2, 2,
                                labels + 1, TN_SPEECH_MAX_FRAMES, NULL);
  if(count != TN_SPEECH_MAX_FRAMES / 2 || labels[0] != 917 || labels[TN_SPEECH_MAX_FRAMES + 1] != 917) return 2;
  for(int i = 1; i <= count; ++i) if(labels[i] != 1) return 3;
  float wide[3 * TN_SPEECH_CLASSES];
  for(int t = 0; t < 3; ++t) for(int c = 0; c < TN_SPEECH_CLASSES; ++c)
    wide[t * TN_SPEECH_CLASSES + c] = c == 1 ? 20.0f : -20.0f;
  if(speech_ctc_decode(wide, 3, TN_SPEECH_CLASSES, 128, labels + 1, 3, NULL) != 1) return 4;
  for(int i = 0; i < 30; ++i){
    wide[0] = NAN;
    if(speech_ctc_decode(wide, 3, TN_SPEECH_CLASSES, 128, labels + 1, 3, NULL) != -1) return 5;
  }
  SpeechLanguage* language = speech_language_create();
  int64_t text[TN_SPEECH_MAX_LABELS];
  for(int i = 0; i < TN_SPEECH_MAX_LABELS; ++i) text[i] = 1 + i % 28;
  if(!language || speech_language_train(language, text, TN_SPEECH_MAX_LABELS)) return 6;
  uint64_t before = speech_language_tokens(language);
  text[100] = 0;
  if(speech_language_train(language, text, TN_SPEECH_MAX_LABELS) != -1 || speech_language_tokens(language) != before) return 7;
  uint16_t prefix[3] = {3, 4, 28};
  if(!isfinite(speech_language_logp(language, prefix, 3, 1))) return 8;
  prefix[2] = 29;
  if(speech_language_logp(language, prefix, 3, 1) != -INFINITY) return 9;
  wide[0] = -20.0f;
  if(speech_ctc_decode_language(wide, 3, TN_SPEECH_CLASSES, 128, language, .5, .3, labels + 1, 3, NULL) != 1) return 10;
  if(speech_ctc_decode_language(wide, 3, TN_SPEECH_CLASSES, 128, language, .5, .3, labels + 1, 0, NULL) != -1) return 11;
  speech_language_free(language);
  free(logits);
  free(labels);
  return 0;
}
