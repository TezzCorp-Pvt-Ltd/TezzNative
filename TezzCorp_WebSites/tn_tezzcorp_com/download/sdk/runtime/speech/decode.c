/* CTC prefix beam search: sum blank/nonblank alignment probabilities.
 * Optional native aggregate character prior; no transcript table or external decoder.
 * See Hannun et al. (2014), https://arxiv.org/abs/1408.2873 . */
#include "speech.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  int length;
  uint64_t hash;
  double blank, nonblank, total, language, rank;
  uint16_t* labels;
} Prefix;

static double add_logs(double a, double b){
  if(a == -INFINITY) return b;
  if(b == -INFINITY) return a;
  double hi = a > b ? a : b, lo = a > b ? b : a;
  return hi + log1p(exp(lo - hi));
}

static int compare_prefix(const void* left, const void* right){
  const Prefix* a = left;
  const Prefix* b = right;
  if(a->rank != b->rank) return a->rank > b->rank ? -1 : 1;
  int length = a->length < b->length ? a->length : b->length;
  for(int i = 0; i < length; ++i){
    if(a->labels[i] != b->labels[i]) return a->labels[i] < b->labels[i] ? -1 : 1;
  }
  return (a->length > b->length) - (a->length < b->length);
}

static Prefix* candidate(Prefix* candidates, int* count, uint16_t* storage, int frames,
                         int* table, unsigned mask, const Prefix* base, int append,
                         const SpeechLanguage* language){
  int length = base->length + (append != 0);
  uint64_t hash = append ? (base->hash ^ (unsigned)append) * UINT64_C(1099511628211) : base->hash;
  unsigned slot = (unsigned)(hash ^ (hash >> 32)) & mask;
  while(table[slot] >= 0){
    Prefix* found = candidates + table[slot];
    if(found->hash == hash && found->length == length &&
       !memcmp(found->labels, base->labels, (size_t)base->length * sizeof(uint16_t)) &&
       (!append || found->labels[length - 1] == append)) return found;
    slot = (slot + 1) & mask;
  }
  Prefix* value = candidates + *count;
  table[slot] = (*count)++;
  value->length = length;
  value->hash = hash;
  value->blank = value->nonblank = value->total = -INFINITY;
  value->language = base->language;
  if(append && language) value->language += speech_language_logp(language, base->labels, base->length, append);
  value->labels = storage + (size_t)table[slot] * frames;
  memcpy(value->labels, base->labels, (size_t)base->length * sizeof(uint16_t));
  if(append) value->labels[length - 1] = (uint16_t)append;
  return value;
}

int speech_ctc_decode(const float* logits, int frames, int classes, int width,
                      int* labels, int capacity, double* log_probability){
  return speech_ctc_decode_language(logits, frames, classes, width, NULL, 0, 0,
                                    labels, capacity, log_probability);
}

int speech_ctc_decode_language(const float* logits, int frames, int classes, int width,
  const SpeechLanguage* language, double weight, double insertion,
  int* labels, int capacity, double* log_probability){
  if(!logits || !labels || frames < 1 || frames > TN_SPEECH_MAX_FRAMES ||
     classes < 2 || classes > TN_SPEECH_CLASSES || width < 1 || width > 128 ||
     capacity < 0 || capacity > TN_SPEECH_MAX_FRAMES || !isfinite(weight) || weight < 0 || weight > 2 ||
     !isfinite(insertion) || insertion < 0 || insertion > 5 ||
     (weight > 0 && !speech_language_tokens(language))) return -1;
  if(weight == 0) language = NULL;
  /* Each retained prefix contributes itself and at most classes-1 extensions. */
  int maximum = width * classes;
  unsigned buckets = 1;
  while(buckets < (unsigned)maximum * 2) buckets <<= 1;
  Prefix* beam = calloc((size_t)width, sizeof(Prefix));
  Prefix* next = calloc((size_t)maximum, sizeof(Prefix));
  uint16_t* beam_labels = calloc((size_t)width * frames, sizeof(uint16_t));
  uint16_t* next_labels = malloc((size_t)maximum * frames * sizeof(uint16_t));
  int* table = malloc((size_t)buckets * sizeof(int));
  int result = -1;
  if(!beam || !next || !beam_labels || !next_labels || !table) goto done;
  beam[0].labels = beam_labels;
  beam[0].hash = UINT64_C(1469598103934665603);
  beam[0].nonblank = -INFINITY;
  int count = 1;
  for(int t = 0; t < frames; ++t){
    double logp[TN_SPEECH_CLASSES], largest = -INFINITY, sum = 0;
    for(int c = 0; c < classes; ++c){
      double value = logits[(size_t)t * classes + c];
      if(!isfinite(value)) goto done;
      if(value > largest) largest = value;
    }
    for(int c = 0; c < classes; ++c) sum += exp((double)logits[(size_t)t * classes + c] - largest);
    double normalizer = log(sum);
    for(int c = 0; c < classes; ++c)
      logp[c] = ((double)logits[(size_t)t * classes + c] - largest) - normalizer;
    for(unsigned i = 0; i < buckets; ++i) table[i] = -1;
    int next_count = 0;
    for(int b = 0; b < count; ++b){
      Prefix* base = beam + b;
      Prefix* same = candidate(next, &next_count, next_labels, frames, table, buckets - 1, base, 0, language);
      same->blank = add_logs(same->blank, base->total + logp[0]);
      int last = base->length ? base->labels[base->length - 1] : 0;
      for(int c = 1; c < classes; ++c){
        if(c == last) same->nonblank = add_logs(same->nonblank, base->nonblank + logp[c]);
        double prefix_probability = c == last ? base->blank : base->total;
        if(prefix_probability == -INFINITY) continue;
        Prefix* extended = candidate(next, &next_count, next_labels, frames, table, buckets - 1, base, c, language);
        extended->nonblank = add_logs(extended->nonblank, prefix_probability + logp[c]);
      }
    }
    for(int i = 0; i < next_count; ++i){
      next[i].total = add_logs(next[i].blank, next[i].nonblank);
      next[i].rank = next[i].total + weight * next[i].language + insertion * next[i].length;
    }
    qsort(next, (size_t)next_count, sizeof(Prefix), compare_prefix);
    count = next_count < width ? next_count : width;
    for(int i = 0; i < count; ++i){
      beam[i] = next[i];
      beam[i].labels = beam_labels + (size_t)i * frames;
      memcpy(beam[i].labels, next[i].labels, (size_t)next[i].length * sizeof(uint16_t));
    }
  }
  if(beam[0].length > capacity) goto done;
  result = beam[0].length;
  for(int i = 0; i < result; ++i) labels[i] = beam[0].labels[i];
  if(log_probability) *log_probability = beam[0].total;
done:
  free(beam); free(next); free(beam_labels); free(next_labels); free(table);
  return result;
}
