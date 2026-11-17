#pragma once

// Validate counts before signed index arithmetic or conversion to byte sizes.
static int tz_cpu_vision_shape(int64_t a, int64_t b, int64_t c, int64_t d){
  int64_t count = 1;
  const int64_t dims[4] = {a, b, c, d};
  const int64_t limit = PTRDIFF_MAX / (int64_t)sizeof(float);
  for(int i = 0; i < 4; i++){
    if(dims[i] <= 0 || count > limit / dims[i]) return 0;
    count *= dims[i];
  }
  return 1;
}

static int tz_cpu_window_size(int64_t input, int64_t kernel, int64_t pad,
                              int64_t stride, int64_t *output){
  if(input <= 0 || kernel <= 0 || pad < 0 || stride <= 0 || pad > (INT64_MAX - input) / 2) return 0;
  int64_t padded = input + 2 * pad;
  if(kernel > padded) return 0;
  *output = (padded - kernel) / stride + 1;
  return 1;
}

static int tz_cpu_conv2d_shape(int64_t B, int64_t InC, int64_t H, int64_t W,
                                int64_t OutC, int64_t kH, int64_t kW,
                                int64_t pad, int64_t stride, int64_t *outH, int64_t *outW){
  return tz_cpu_window_size(H, kH, pad, stride, outH) &&
         tz_cpu_window_size(W, kW, pad, stride, outW) &&
         tz_cpu_vision_shape(B, InC, H, W) &&
         tz_cpu_vision_shape(OutC, InC, kH, kW) &&
         tz_cpu_vision_shape(B, OutC, *outH, *outW);
}

// ── 9. conv2d_f32 ─────────────────────────────────────────────────────────────
// Direct 2D convolution: out[B, OutC, outH, outW] = conv(x[B, InC, H, W], W[OutC, InC, kH, kW]) + b[OutC]
// outH = (H + 2*pad - kH)/stride + 1, outW = (W + 2*pad - kW)/stride + 1
TZ_CPU_EXPORT int64_t tz_cpu_conv2d_f32(int64_t x, int64_t W, int64_t b, int64_t out,
                                         int64_t B, int64_t InC, int64_t H, int64_t W_in,
                                         int64_t OutC, int64_t kH, int64_t kW,
                                         int64_t pad, int64_t stride){
  float *xp = fp(x), *wp = fp(W), *bp = b ? fp(b) : NULL, *op = fp(out);
  int64_t outH, outW;
  if(!xp || !wp || !op || !tz_cpu_conv2d_shape(B, InC, H, W_in, OutC, kH, kW, pad, stride, &outH, &outW)) return -1;
  int64_t bi;
#ifdef _OPENMP
  #pragma omp parallel for schedule(dynamic, 4)
#endif
  for(bi = 0; bi < B; bi++){
    for(int64_t oc = 0; oc < OutC; oc++){
      float bias = bp ? bp[oc] : 0.0f;
      float *out_plane = op + (bi * OutC + oc) * (outH * outW);
      for(int64_t oh = 0; oh < outH; oh++){
        int64_t ih_base = oh * stride - pad;
        for(int64_t ow = 0; ow < outW; ow++){
          int64_t iw_base = ow * stride - pad;
          float acc = bias;
          for(int64_t ic = 0; ic < InC; ic++){
            const float *in_plane = xp + (bi * InC + ic) * (H * W_in);
            const float *wt_plane = wp + (oc * InC + ic) * (kH * kW);
            for(int64_t kh = 0; kh < kH; kh++){
              int64_t ih = ih_base + kh;
              if((uint64_t)ih < (uint64_t)H){
                for(int64_t kw = 0; kw < kW; kw++){
                  int64_t iw = iw_base + kw;
                  if((uint64_t)iw < (uint64_t)W_in){
                    acc += in_plane[ih * W_in + iw] * wt_plane[kh * kW + kw];
                  }
                }
              }
            }
          }
          out_plane[oh * outW + ow] = acc;
        }
      }
    }
  }
  return 0;
}

// ── 10. conv2d_bwd_f32 ────────────────────────────────────────────────────────
// Backward pass: computes dx[B, InC, H, W], dW[OutC, InC, kH, kW], db[OutC] from dout[B, OutC, outH, outW]
TZ_CPU_EXPORT int64_t tz_cpu_conv2d_bwd_f32(int64_t dout, int64_t x, int64_t W,
                                             int64_t dx, int64_t dW, int64_t db,
                                             int64_t B, int64_t InC, int64_t H, int64_t W_in,
                                             int64_t OutC, int64_t kH, int64_t kW,
                                             int64_t pad, int64_t stride){
  float *dop = fp(dout), *xp = fp(x), *wp = fp(W);
  float *dxp = dx ? fp(dx) : NULL;
  float *dwp = dW ? fp(dW) : NULL;
  float *dbp = db ? fp(db) : NULL;
  int64_t outH, outW;
  if(!dop || !xp || !wp || !tz_cpu_conv2d_shape(B, InC, H, W_in, OutC, kH, kW, pad, stride, &outH, &outW)) return -1;

  if(dxp) memset(dxp, 0, (size_t)(B * InC * H * W_in * sizeof(float)));

  for(int64_t bi = 0; bi < B; bi++){
    for(int64_t oc = 0; oc < OutC; oc++){
      const float *do_plane = dop + (bi * OutC + oc) * (outH * outW);
      for(int64_t oh = 0; oh < outH; oh++){
        int64_t ih_base = oh * stride - pad;
        for(int64_t ow = 0; ow < outW; ow++){
          float g = do_plane[oh * outW + ow];
          int64_t iw_base = ow * stride - pad;
          if(dbp) dbp[oc] += g;
          for(int64_t ic = 0; ic < InC; ic++){
            const float *in_plane = xp + (bi * InC + ic) * (H * W_in);
            float *dw_plane = dwp ? (dwp + (oc * InC + ic) * (kH * kW)) : NULL;
            const float *w_plane = wp + (oc * InC + ic) * (kH * kW);
            float *dx_plane = dxp ? (dxp + (bi * InC + ic) * (H * W_in)) : NULL;
            for(int64_t kh = 0; kh < kH; kh++){
              int64_t ih = ih_base + kh;
              if((uint64_t)ih < (uint64_t)H){
                for(int64_t kw = 0; kw < kW; kw++){
                  int64_t iw = iw_base + kw;
                  if((uint64_t)iw < (uint64_t)W_in){
                    if(dw_plane) dw_plane[kh * kW + kw] += g * in_plane[ih * W_in + iw];
                    if(dx_plane) dx_plane[ih * W_in + iw] += g * w_plane[kh * kW + kw];
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  return 0;
}

// ── 11. maxpool2d_f32 ─────────────────────────────────────────────────────────
// Max pooling: out[B, C, outH, outW], mask stores winning index for backprop
TZ_CPU_EXPORT int64_t tz_cpu_maxpool2d_f32(int64_t x, int64_t out, int64_t mask,
                                            int64_t B, int64_t C, int64_t H, int64_t W,
                                            int64_t kH, int64_t kW, int64_t stride){
  float *xp = fp(x), *op = fp(out);
  int32_t *mp = mask ? ip(mask) : NULL;
  int64_t outH, outW;
  if(!xp || !op || !tz_cpu_window_size(H, kH, 0, stride, &outH) ||
     !tz_cpu_window_size(W, kW, 0, stride, &outW) ||
     !tz_cpu_vision_shape(B, C, H, W) || !tz_cpu_vision_shape(B, C, outH, outW) ||
     (mp && H * W > INT32_MAX)) return -1;

  for(int64_t bc = 0; bc < B * C; bc++){
    const float *in_plane = xp + bc * (H * W);
    float *out_plane = op + bc * (outH * outW);
    int32_t *mask_plane = mp ? (mp + bc * (outH * outW)) : NULL;
    for(int64_t oh = 0; oh < outH; oh++){
      for(int64_t ow = 0; ow < outW; ow++){
        int64_t best_idx = oh * stride * W + ow * stride;
        float mx = in_plane[best_idx];
        for(int64_t kh = 0; kh < kH; kh++){
          int64_t ih = oh * stride + kh;
          for(int64_t kw = 0; kw < kW; kw++){
            int64_t iw = ow * stride + kw;
            int64_t idx = ih * W + iw;
            if(in_plane[idx] > mx){ mx = in_plane[idx]; best_idx = idx; }
          }
        }
        out_plane[oh * outW + ow] = mx;
        if(mask_plane) mask_plane[oh * outW + ow] = (int32_t)best_idx;
      }
    }
  }
  return 0;
}

// Backward pass for MaxPool2D: unpools dout into dx using argmax indices stored in mask
TZ_CPU_EXPORT int64_t tz_cpu_maxpool2d_bwd_f32(int64_t dout, int64_t mask, int64_t dx,
                                                int64_t B, int64_t C, int64_t H, int64_t W,
                                                int64_t outH, int64_t outW){
  float *dop = fp(dout), *dxp = fp(dx);
  const int32_t *mp = ip(mask);
  if(!dxp || !dop || !mp || !tz_cpu_vision_shape(B, C, H, W) ||
     !tz_cpu_vision_shape(B, C, outH, outW) || H * W > INT32_MAX) return -1;
  memset(dxp, 0, (size_t)(B * C * H * W * sizeof(float)));

  for(int64_t bc = 0; bc < B * C; bc++){
    const float *do_plane = dop + bc * (outH * outW);
    const int32_t *m_plane = mp + bc * (outH * outW);
    float *dx_plane = dxp + bc * (H * W);
    for(int64_t i = 0; i < outH * outW; i++){
      int32_t best_idx = m_plane[i];
      if((uint64_t)best_idx < (uint64_t)(H * W)){
        dx_plane[best_idx] += do_plane[i];
      }
    }
  }
  return 0;
}

// ── 12. topk_f32 ──────────────────────────────────────────────────────────────
// Finds top-K values and indices from x[n]
TZ_CPU_EXPORT int64_t tz_cpu_topk_f32(int64_t x, int64_t out_vals, int64_t out_indices,
                                       int64_t n, int64_t k){
  float *xp = fp(x), *ov = fp(out_vals);
  int32_t *oi = ip(out_indices);
  if(n < 0 || k < 0 || n > PTRDIFF_MAX / (int64_t)sizeof(float) || (oi && n > INT32_MAX)) return -1;
  if(k > n) k = n;
  if(k == 0) return 0;
  if(!xp || (!ov && !oi)) return -1;

  // Track chosen indices
  char *chosen = (char*)calloc((size_t)n, 1);
  if(!chosen) return -1;

  for(int64_t ki = 0; ki < k; ki++){
    float mx = 0.0f;
    int64_t best_j = -1;
    for(int64_t j = 0; j < n; j++){
      if(!chosen[j] && (best_j < 0 || xp[j] > mx)){ mx = xp[j]; best_j = j; }
    }
    if(best_j >= 0){
      chosen[best_j] = 1;
      if(ov) ov[ki] = mx;
      if(oi) oi[ki] = (int32_t)best_j;
    }
  }
  free(chosen);
  return 0;
}

// ── 13. sample_f32 ────────────────────────────────────────────────────────────
// Temperature + Top-P / Top-K nucleus sampling from logits[n]
// seed is stateful; returns selected token ID (0..n-1)
static uint64_t s_rng_state = 88172645463325252ULL;
TZ_CPU_EXPORT int64_t tz_cpu_sample_f32(int64_t logits, int64_t n,
                                         double temperature, double top_p, int64_t top_k,
                                         int64_t seed){
  float *lp = fp(logits);
  if(n <= 0) return 0;
  if(seed > 0) s_rng_state = (uint64_t)seed;

  float temp = (float)temperature;
  if(temp <= 1e-4f){
    // Greedy argmax
    int64_t best = 0;
    for(int64_t i = 1; i < n; i++) if(lp[i] > lp[best]) best = i;
    return best;
  }

  // Softmax with temperature
  float *probs = (float*)malloc((size_t)n * sizeof(float));
  if(!probs) return 0;
  float mx = lp[0] / temp;
  for(int64_t i = 1; i < n; i++) if(lp[i] / temp > mx) mx = lp[i] / temp;
  float sum = 0.0f;
  for(int64_t i = 0; i < n; i++){ probs[i] = expf(lp[i] / temp - mx); sum += probs[i]; }
  float inv_sum = (sum > 0.0f) ? 1.0f / sum : 0.0f;
  for(int64_t i = 0; i < n; i++) probs[i] *= inv_sum;

  // Xorshift64 random float in [0, 1)
  s_rng_state ^= (s_rng_state << 13);
  s_rng_state ^= (s_rng_state >> 7);
  s_rng_state ^= (s_rng_state << 17);
  float r = (float)(s_rng_state & 0xFFFFFFFFFFF) / (float)0x100000000000;

  // Cumulative distribution sampling
  float cum = 0.0f;
  int64_t choice = n - 1;
  for(int64_t i = 0; i < n; i++){
    cum += probs[i];
    if(r <= cum){ choice = i; break; }
  }
  free(probs);
  return choice;
}

// ── 14. kv_cache_update_f32 ───────────────────────────────────────────────────
// Rolling KV-cache insert: cache[B, H, max_seq, D] at position `pos`
// k_new/v_new: [B, H, 1, D]
TZ_CPU_EXPORT int64_t tz_cpu_kv_cache_update_f32(int64_t cache, int64_t new_val,
                                                  int64_t B, int64_t H, int64_t max_seq,
                                                  int64_t D, int64_t pos){
  float *c = fp(cache), *v = fp(new_val);
  if(!c || !v || pos < 0 || pos >= max_seq || !tz_cpu_vision_shape(B, H, max_seq, D)) return -1;
  for(int64_t b = 0; b < B; b++){
    for(int64_t h = 0; h < H; h++){
      float *dst = c + ((b * H + h) * max_seq + pos) * D;
      const float *src = v + (b * H + h) * D;
      memcpy(dst, src, (size_t)D * sizeof(float));
    }
  }
  return 0;
}

// ── 15. masked_softmax_f32 ────────────────────────────────────────────────────
// Softmax over enabled entries only. Disabled entries and all-masked rows are zero.
TZ_CPU_EXPORT int64_t tz_cpu_masked_softmax_f32(int64_t x, int64_t mask, int64_t out,
                                                 int64_t rows, int64_t cols){
  float *xp = fp(x), *op = fp(out);
  const int32_t *mp = mask ? ip(mask) : NULL;
  if(!xp || !op || !tz_cpu_vision_shape(rows, cols, 1, 1)) return -1;
  for(int64_t r = 0; r < rows; r++){
    const float *xrow = xp + r * cols;
    float *orow = op + r * cols;
    const int32_t *mrow = mp ? (mp + r * cols) : NULL;
    float mx = -INFINITY;
    for(int64_t c = 0; c < cols; c++){
      if((!mrow || mrow[c]) && xrow[c] > mx) mx = xrow[c];
    }
    float s = 0.0f;
    for(int64_t c = 0; c < cols; c++){
      orow[c] = (mrow && !mrow[c]) ? 0.0f : expf(xrow[c] - mx);
      s += orow[c];
    }
    float inv = (s > 0.0f) ? 1.0f / s : 0.0f;
    for(int64_t c = 0; c < cols; c++) orow[c] *= inv;
  }
  return 0;
}

// ── 16. concat_f32 & split_f32 ────────────────────────────────────────────────
// Concatenate 2 tensors along dimension 1: A[M, N1], B[M, N2] -> Out[M, N1 + N2]
TZ_CPU_EXPORT int64_t tz_cpu_concat_f32(int64_t A, int64_t B, int64_t out,
                                         int64_t M, int64_t N1, int64_t N2){
  float *pa = fp(A), *pb = fp(B), *po = fp(out);
  int64_t outN = N1 + N2;
  for(int64_t m = 0; m < M; m++){
    memcpy(po + m * outN,      pa + m * N1, (size_t)N1 * sizeof(float));
    memcpy(po + m * outN + N1, pb + m * N2, (size_t)N2 * sizeof(float));
  }
  return 0;
}

// Split tensor along dimension 1: In[M, N1 + N2] -> A[M, N1], B[M, N2]
TZ_CPU_EXPORT int64_t tz_cpu_split_f32(int64_t in, int64_t A, int64_t B,
                                        int64_t M, int64_t N1, int64_t N2){
  float *pi = fp(in), *pa = fp(A), *pb = fp(B);
  int64_t inN = N1 + N2;
  for(int64_t m = 0; m < M; m++){
    if(pa) memcpy(pa + m * N1, pi + m * inN,      (size_t)N1 * sizeof(float));
    if(pb) memcpy(pb + m * N2, pi + m * inN + N1, (size_t)N2 * sizeof(float));
  }
  return 0;
}

// ── 17. FP16 half-precision conversion ────────────────────────────────────────
// Single-precision float32 <-> IEEE 754 half-precision float16 (uint16_t)
static uint16_t f32_to_f16_val(float val){ return tn_f32_to_f16(val); }
static float f16_to_f32_val(uint16_t val){ return tn_f16_to_f32(val); }

TZ_CPU_EXPORT int64_t tz_cpu_f32_to_f16(int64_t src, int64_t dst, int64_t n){
  float *sp = fp(src); uint16_t *dp = (uint16_t*)(uintptr_t)(uint64_t)dst;
  for(int64_t i = 0; i < n; i++) dp[i] = f32_to_f16_val(sp[i]);
  return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_f16_to_f32(int64_t src, int64_t dst, int64_t n){
  uint16_t *sp = (uint16_t*)(uintptr_t)(uint64_t)src; float *dp = fp(dst);
  for(int64_t i = 0; i < n; i++) dp[i] = f16_to_f32_val(sp[i]);
  return 0;
}

// ── 18. int8_gemm (quantized matrix multiply with scale) ──────────────────────
// C[M, N] (f32) = (A[M, K] (int8) @ B[K, N] (int8)) * (scaleA * scaleB)
TZ_CPU_EXPORT int64_t tz_cpu_int8_gemm(int64_t A, int64_t B, int64_t C,
                                        int64_t M, int64_t K, int64_t N,
                                        double scaleA, double scaleB){
  const int8_t *pA = (const int8_t*)(uintptr_t)(uint64_t)A;
  const int8_t *pB = (const int8_t*)(uintptr_t)(uint64_t)B;
  float *pC = fp(C);
  float s = (float)(scaleA * scaleB);
  int m;
#ifdef _OPENMP
  #pragma omp parallel for schedule(dynamic, 8)
#endif
  for(m = 0; m < (int)M; m++){
    for(int64_t n = 0; n < N; n++){
      int32_t acc = 0;
      for(int64_t k = 0; k < K; k++) acc += (int32_t)pA[m * K + k] * (int32_t)pB[k * N + n];
      pC[m * N + n] = (float)acc * s;
    }
  }
  return 0;
}
