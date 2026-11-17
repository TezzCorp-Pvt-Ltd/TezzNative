/* Exercise the production kernels directly, without a replacement runtime. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <float.h>
#include "float_bits.h"
#define TZ_CPU_EXPORT
static float *fp(int64_t p) { return (float *)(uintptr_t)p; }
static int32_t *ip(int64_t p) { return (int32_t *)(uintptr_t)p; }
#include "tzcpu/vision.h"

#define P(value) ((int64_t)(uintptr_t)(value))
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); return 1; } } while (0)
static int close_value(double actual, double expected) {
  return isfinite(actual) && fabs(actual - expected) <= 0.0002 * fmax(1.0, fabs(expected));
}

static int maxpool_extreme(void) {
  float input[] = {-FLT_MAX, -2e30f, -INFINITY, -3e30f}, out = 13;
  int32_t mask = -7;
  CHECK(tz_cpu_maxpool2d_f32(P(input), P(&out), P(&mask), 1, 1, 2, 2, 2, 2, 1) == 0);
  CHECK(out == input[1] && mask == 1);
  return 0;
}

static int maxpool_backward(void) {
  float input[] = {0, 1, 0, 1, 9, 1, 0, 1, 0};
  float output[4], grad[] = {1, 2, 3, 4}, dx[9];
  int32_t mask[4];
  CHECK(tz_cpu_maxpool2d_f32(P(input), P(output), P(mask), 1, 1, 3, 3, 2, 2, 1) == 0);
  CHECK(tz_cpu_maxpool2d_bwd_f32(P(grad), P(mask), P(dx), 1, 1, 3, 3, 2, 2) == 0);
  for (int j = 0; j < 9; ++j) CHECK(dx[j] == (j == 4 ? 10.0f : 0.0f));
  float negative[] = {-INFINITY, -INFINITY, -INFINITY, -INFINITY};
  CHECK(tz_cpu_maxpool2d_f32(P(negative), P(output), P(mask), 1, 1, 2, 2, 1, 2, 1) == 0);
  CHECK(output[0] == -INFINITY && output[1] == -INFINITY && mask[0] == 0 && mask[1] == 2);
  return 0;
}

static int topk_extreme(void) {
  float input[] = {-FLT_MAX, -2e30f, -INFINITY, -2e30f};
  float out[] = {13, 13, 13, 13, 17};
  int32_t index[] = {-7, -7, -7, -7, -7}, expected[] = {1, 3, 0, 2};
  CHECK(tz_cpu_topk_f32(P(input), P(out), P(index), 4, 7) == 0);
  for (int j = 0; j < 4; ++j) CHECK(index[j] == expected[j] && out[j] == input[expected[j]]);
  CHECK(out[4] == 17 && index[4] == -7);
  CHECK(tz_cpu_topk_f32(P(input), 0, P(index), 4, 1) == 0 && index[0] == 1);
  CHECK(tz_cpu_topk_f32(P(input), P(out), 0, 4, 1) == 0 && out[0] == input[1]);
  return 0;
}

static int masked_softmax(void) {
  float input[] = {-2e9f, -2e9f, NAN, 100, 200, 300}, output[6];
  int32_t mask[] = {1, 1, 0, 0, 0, 0};
  CHECK(tz_cpu_masked_softmax_f32(P(input), P(mask), P(output), 2, 3) == 0);
  CHECK(output[0] == 0.5f && output[1] == 0.5f && output[2] == 0.0f);
  CHECK(output[3] == 0.0f && output[4] == 0.0f && output[5] == 0.0f);
  float unmasked[] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
  CHECK(tz_cpu_masked_softmax_f32(P(unmasked), 0, P(unmasked), 1, 3) == 0);
  for (int j = 0; j < 3; ++j) CHECK(close_value(unmasked[j], 1.0 / 3));
  return 0;
}

static int kv_negative(void) {
  float cache[] = {11, 12, 13, 14, 15, 16}, added[] = {91, 92};
  CHECK(tz_cpu_kv_cache_update_f32(P(cache + 2), P(added), 1, 1, 2, 2, -1) != 0);
  for (int j = 0; j < 6; ++j) CHECK(cache[j] == 11 + j);
  CHECK(tz_cpu_kv_cache_update_f32(P(cache + 2), P(added), 1, 1, 2, 2, 1) == 0);
  CHECK(cache[4] == 91 && cache[5] == 92 && cache[0] == 11 && cache[1] == 12);
  return 0;
}

static int stride_zero(const char *kind) {
  float input[4] = {1, 2, 3, 4}, weight = 1, out[4] = {17, 17, 17, 17};
  if (!strcmp(kind, "conv_stride_zero")) {
    CHECK(tz_cpu_conv2d_f32(P(input), P(&weight), 0, P(out), 1, 1, 2, 2, 1, 1, 1, 0, 0) != 0);
  } else if (!strcmp(kind, "conv_backward_stride_zero")) {
    CHECK(tz_cpu_conv2d_bwd_f32(P(input), P(input), P(&weight), P(out), 0, 0, 1, 1, 2, 2, 1, 1, 1, 0, 0) != 0);
  } else {
    CHECK(tz_cpu_maxpool2d_f32(P(input), P(out), 0, 1, 1, 2, 2, 1, 1, 0) != 0);
  }
  for (int j = 0; j < 4; ++j) CHECK(out[j] == 17);
  return 0;
}

/* Scalar double precision oracle; finite differences of this objective are
   independent of the production backward loop. */
static double reference_conv(const double *x, const double *w, const double *b, const double *g, double *y) {
  double loss = 0;
  for (int oc = 0; oc < 2; ++oc) for (int oh = 0; oh < 2; ++oh) for (int ow = 0; ow < 2; ++ow) {
    double sum = b[oc];
    for (int ic = 0; ic < 2; ++ic) for (int kh = 0; kh < 2; ++kh) for (int kw = 0; kw < 3; ++kw) {
      int ih = oh * 2 - 1 + kh, iw = ow * 2 - 1 + kw;
      if (ih >= 0 && ih < 3 && iw >= 0 && iw < 4)
        sum += x[(ic * 3 + ih) * 4 + iw] * w[((oc * 2 + ic) * 2 + kh) * 3 + kw];
    }
    int index = (oc * 2 + oh) * 2 + ow;
    if (y) y[index] = sum;
    loss += sum * g[index];
  }
  return loss;
}

static int convolution_oracle(void) {
  double x[24], w[24], b[] = {0.25, -0.5}, g[8], expected[8];
  float xf[24], wf[24], bf[] = {0.25f, -0.5f}, gf[8], out[8], dx[24], dw[24], db[] = {0.5f, 0.5f};
  for (int j = 0; j < 24; ++j) { x[j] = xf[j] = j * 0.125 - 2; w[j] = wf[j] = (j % 7 - 3) * 0.25; dw[j] = 0.25f; dx[j] = 99; }
  for (int j = 0; j < 8; ++j) g[j] = gf[j] = (j - 3) * 0.125;
  reference_conv(x, w, b, g, expected);
  CHECK(tz_cpu_conv2d_f32(P(xf), P(wf), P(bf), P(out), 1, 2, 3, 4, 2, 2, 3, 1, 2) == 0);
  for (int j = 0; j < 8; ++j) CHECK(close_value(out[j], expected[j]));
  CHECK(tz_cpu_conv2d_bwd_f32(P(gf), P(xf), P(wf), P(dx), P(dw), P(db), 1, 2, 3, 4, 2, 2, 3, 1, 2) == 0);
  for (int group = 0; group < 3; ++group) {
    double *values = group == 0 ? x : group == 1 ? w : b;
    float *actual = group == 0 ? dx : group == 1 ? dw : db;
    for (int j = 0; j < (group == 2 ? 2 : 24); ++j) {
      double saved = values[j], h = 0.0001;
      values[j] = saved + h;
      double plus = reference_conv(x, w, b, g, NULL);
      values[j] = saved - h;
      double minus = reference_conv(x, w, b, g, NULL);
      values[j] = saved;
      CHECK(close_value(actual[j], (plus - minus) / (2 * h) + (group == 0 ? 0 : group == 1 ? 0.25 : 0.5)));
    }
  }
  return 0;
}

static int invalid_dimensions(const char *kind) {
  float value[4] = {17, 17, 17, 17};
  int32_t mask[4] = {0, 0, 0, 0};
  int64_t p = P(value), m = P(mask);
  if (!strcmp(kind, "conv_dimensions")) {
    CHECK(tz_cpu_conv2d_f32(p, p, 0, p, -1, 1, 1, 1, 1, 1, 1, 0, 1) != 0);
    CHECK(tz_cpu_conv2d_f32(p, p, 0, p, 1, 1, 2, 2, 1, 3, 3, 0, 2) != 0);
    CHECK(tz_cpu_conv2d_f32(p, p, 0, p, INT64_MAX, 2, 2, 2, 1, 1, 1, 0, 1) != 0);
    CHECK(tz_cpu_conv2d_f32(p, p, 0, p, 1, 1, 1, 1, 1, 1, 1, INT64_MAX, 1) != 0);
    CHECK(tz_cpu_conv2d_f32(0, p, 0, p, 1, 1, 1, 1, 1, 1, 1, 0, 1) != 0);
  } else if (!strcmp(kind, "conv_backward_dimensions")) {
    CHECK(tz_cpu_conv2d_bwd_f32(p, p, p, p, p, p, 1, -1, 1, 1, 1, 1, 1, 0, 1) != 0);
    CHECK(tz_cpu_conv2d_bwd_f32(p, p, p, p, p, p, INT64_MAX, 2, 2, 2, 1, 1, 1, 0, 1) != 0);
    CHECK(tz_cpu_conv2d_bwd_f32(p, p, p, p, p, p, 1, 1, 1, 1, 1, 1, 1, INT64_MAX, 1) != 0);
    CHECK(tz_cpu_conv2d_bwd_f32(0, p, p, p, p, p, 1, 1, 1, 1, 1, 1, 1, 0, 1) != 0);
  } else if (!strcmp(kind, "pool_dimensions")) {
    CHECK(tz_cpu_maxpool2d_f32(p, p, m, -1, 1, 2, 2, 1, 1, 1) != 0);
    CHECK(tz_cpu_maxpool2d_f32(p, p, m, 1, 1, 2, 2, 3, 3, 2) != 0);
    CHECK(tz_cpu_maxpool2d_f32(p, p, m, INT64_MAX, 2, 2, 2, 1, 1, 1) != 0);
    CHECK(tz_cpu_maxpool2d_f32(p, p, m, 1, 1, 65536, 65536, 1, 1, 1) != 0);
    CHECK(tz_cpu_maxpool2d_f32(0, p, m, 1, 1, 1, 1, 1, 1, 1) != 0);
  } else if (!strcmp(kind, "pool_backward_dimensions")) {
    CHECK(tz_cpu_maxpool2d_bwd_f32(p, m, p, -1, 1, 2, 2, 1, 1) != 0);
    CHECK(tz_cpu_maxpool2d_bwd_f32(p, m, p, 1, 1, 2, 2, INT64_MAX, 2) != 0);
    CHECK(tz_cpu_maxpool2d_bwd_f32(p, m, p, 1, 1, 65536, 65536, 1, 1) != 0);
  } else if (!strcmp(kind, "topk_dimensions")) {
    CHECK(tz_cpu_topk_f32(p, p, m, 4, -1) != 0);
    CHECK(tz_cpu_topk_f32(p, p, m, -1, 1) != 0);
    CHECK(tz_cpu_topk_f32(p, p, m, INT64_MAX, 1) != 0);
    CHECK(tz_cpu_topk_f32(p, p, m, (int64_t)INT32_MAX + 2, 1) != 0);
    CHECK(tz_cpu_topk_f32(0, p, m, 1, 1) != 0);
  } else if (!strcmp(kind, "masked_dimensions")) {
    CHECK(tz_cpu_masked_softmax_f32(p, m, p, -1, 1) != 0);
    CHECK(tz_cpu_masked_softmax_f32(p, m, p, 1, 0) != 0);
    CHECK(tz_cpu_masked_softmax_f32(p, m, p, INT64_MAX, 2) != 0);
    CHECK(tz_cpu_masked_softmax_f32(0, m, p, 1, 1) != 0);
  } else {
    CHECK(tz_cpu_kv_cache_update_f32(p, p, -1, 1, 1, 1, 0) != 0);
    CHECK(tz_cpu_kv_cache_update_f32(p, p, INT64_MAX, 2, 1, 1, 0) != 0);
    CHECK(tz_cpu_kv_cache_update_f32(p, p, 1, 1, INT64_MAX, 2, 0) != 0);
    CHECK(tz_cpu_kv_cache_update_f32(0, p, 1, 1, 1, 1, 0) != 0);
  }
  for (int j = 0; j < 4; ++j) CHECK(value[j] == 17);
  return 0;
}

int main(int argc, char **argv) {
  if (argc != 2) return 2;
  const char *name = argv[1];
  if (!strcmp(name, "maxpool_extreme")) return maxpool_extreme();
  if (!strcmp(name, "maxpool_backward")) return maxpool_backward();
  if (!strcmp(name, "topk_extreme")) return topk_extreme();
  if (!strcmp(name, "masked_softmax")) return masked_softmax();
  if (!strcmp(name, "kv_negative")) return kv_negative();
  if (strstr(name, "stride_zero")) return stride_zero(name);
  if (!strcmp(name, "convolution_oracle")) return convolution_oracle();
  if (strstr(name, "dimensions")) return invalid_dimensions(name);
  return 2;
}
