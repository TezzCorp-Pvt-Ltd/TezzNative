/* Own implementation of CTC forward/backward and Viterbi alignment.
 * Algorithm: Graves et al., ICML 2006, https://www.cs.toronto.edu/~graves/icml_2006.pdf
 * The blank symbol is 0. No external model, decoder, or ML framework. */
#include "speech.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static double logadd(double a, double b){
  if(a == -INFINITY) return b;
  if(b == -INFINITY) return a;
  double hi = a > b ? a : b, lo = a > b ? b : a;
  return hi + log1p(exp(lo - hi));
}

static int symbol(const int64_t* labels, int s){ return (s & 1) ? (int)labels[s / 2] : 0; }

int speech_ctc(const float* logits, int frames, int classes, const int64_t* labels,
               int count, float* gradient, int* alignment, double* loss){
  if(!logits || !loss || frames < 1 || frames > TN_SPEECH_MAX_FRAMES || classes < 2 || classes > 512 ||
      count < 0 || count > TN_SPEECH_MAX_LABELS || (count && !labels) || (alignment && count == 0)) return -1;
  int minimum = count;
  for(int i = 0; i < count; ++i){
    if(labels[i] <= 0 || labels[i] >= classes) return -1;
    if(i && labels[i] == labels[i - 1]) minimum++;
  }
  if(frames < minimum) return -1;
  int states = count * 2 + 1;
  size_t cells = (size_t)frames * states;
  double* alpha = (double*)malloc(cells * sizeof(double));
  double* logp = (double*)malloc((size_t)frames * classes * sizeof(double));
  double* beta = (double*)malloc((size_t)states * sizeof(double));
  double* next = (double*)malloc((size_t)states * sizeof(double));
  unsigned char* back = alignment ? (unsigned char*)malloc(cells) : NULL;
  int status = -1;
  if(!alpha || !logp || !beta || !next || (alignment && !back)) goto done;
  for(int t = 0; t < frames; ++t){
    double max = -INFINITY, sum = 0.0;
    for(int c = 0; c < classes; ++c){
      float value = logits[(size_t)t * classes + c];
      if(!isfinite(value)) goto done;
      if(value > max) max = value;
    }
    for(int c = 0; c < classes; ++c) sum += exp(logits[(size_t)t * classes + c] - max);
    /* Subtract the maximum before the small logarithm: adding log(sum) to a
     * large common offset loses it, producing probabilities greater than one. */
    double normalizer = log(sum);
    for(int c = 0; c < classes; ++c)
      logp[(size_t)t * classes + c] = ((double)logits[(size_t)t * classes + c] - max) - normalizer;
  }
  for(size_t i = 0; i < cells; ++i) alpha[i] = -INFINITY;
  alpha[0] = logp[0];
  if(count) alpha[1] = logp[labels[0]];
  for(int t = 1; t < frames; ++t){
    for(int s = 0; s < states; ++s){
      double value = alpha[(size_t)(t - 1) * states + s];
      if(s > 0) value = logadd(value, alpha[(size_t)(t - 1) * states + s - 1]);
      int label = symbol(labels, s);
      if(s > 1 && label && label != symbol(labels, s - 2))
        value = logadd(value, alpha[(size_t)(t - 1) * states + s - 2]);
      alpha[(size_t)t * states + s] = value + logp[(size_t)t * classes + label];
    }
  }
  double logz = alpha[(size_t)(frames - 1) * states + states - 1];
  if(count) logz = logadd(logz, alpha[(size_t)(frames - 1) * states + states - 2]);
  if(!isfinite(logz)) goto done;
  *loss = -logz;
  if(gradient){
    for(size_t i = 0; i < (size_t)frames * classes; ++i) gradient[i] = (float)exp(logp[i]);
    for(int s = 0; s < states; ++s) beta[s] = -INFINITY;
    beta[states - 1] = 0;
    if(count) beta[states - 2] = 0;
    for(int t = frames - 1; t >= 0; --t){
      for(int s = 0; s < states; ++s){
        int label = symbol(labels, s);
        gradient[(size_t)t * classes + label] -= (float)exp(alpha[(size_t)t * states + s] + beta[s] - logz);
      }
      if(t == 0) break;
      for(int s = 0; s < states; ++s){
        double value = beta[s] + logp[(size_t)t * classes + symbol(labels, s)];
        if(s + 1 < states) value = logadd(value, beta[s + 1] + logp[(size_t)t * classes + symbol(labels, s + 1)]);
        if(s + 2 < states && symbol(labels, s + 2) && symbol(labels, s) != symbol(labels, s + 2))
          value = logadd(value, beta[s + 2] + logp[(size_t)t * classes + symbol(labels, s + 2)]);
        next[s] = value;
      }
      double* swap = beta; beta = next; next = swap;
    }
  }
  if(alignment){
    for(size_t i = 0; i < cells; ++i) alpha[i] = -INFINITY;
    alpha[0] = logp[0];
    if(count) alpha[1] = logp[labels[0]];
    for(int t = 1; t < frames; ++t){
      for(int s = 0; s < states; ++s){
        double best = alpha[(size_t)(t - 1) * states + s];
        unsigned char step = 0;
        if(s > 0 && alpha[(size_t)(t - 1) * states + s - 1] > best){ best = alpha[(size_t)(t - 1) * states + s - 1]; step = 1; }
        if(s > 1 && symbol(labels, s) && symbol(labels, s) != symbol(labels, s - 2) && alpha[(size_t)(t - 1) * states + s - 2] > best){
          best = alpha[(size_t)(t - 1) * states + s - 2]; step = 2;
        }
        alpha[(size_t)t * states + s] = best + logp[(size_t)t * classes + symbol(labels, s)];
        back[(size_t)t * states + s] = step;
      }
    }
    int state = states - 1;
    if(alpha[(size_t)(frames - 1) * states + states - 2] > alpha[(size_t)(frames - 1) * states + state]) state--;
    for(int t = frames - 1; t >= 0; --t){
      alignment[t] = state & 1 ? state / 2 : (state ? state / 2 - 1 : 0);
      if(t) state -= back[(size_t)t * states + state];
    }
  }
  status = 0;
done:
  free(alpha); free(logp); free(beta); free(next); free(back);
  return status;
}
