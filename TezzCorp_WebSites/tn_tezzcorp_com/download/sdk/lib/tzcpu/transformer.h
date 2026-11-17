#pragma once

// ── Loss reporting (reads float buffer, prints real loss value) ───────────────
// Computes mean of loss_buf[0..n), prints "[Epoch %d] loss = %.4f", 
// returns (int)(mean_loss * 10000) so Tezz can compare it as an integer.
TZ_CPU_EXPORT int64_t tz_cpu_report_loss(int64_t buf, int64_t n, int64_t epoch){
  float *p = fp(buf);
  float sum = 0.f;
  for(int64_t i = 0; i < n; i++) sum += p[i];
  float mean = sum / (float)n;
  printf("[Epoch %2lld] loss = %.6f\n", (long long)epoch, mean);
  fflush(stdout);
  return (int64_t)(mean * 10000.0f);  // return scaled int for comparison in Tezz
}

// ── Cycling label fill (i32, for diverse synthetic data) ─────────────────────
// Fills buf[0..n) with int32 labels: 0,1,2,...,classes-1,0,1,...
// Ensures every class appears, giving the network a real learning signal.
TZ_CPU_EXPORT int64_t tz_cpu_fill_labels_cycle_i32(int64_t buf, int64_t n, int64_t classes){
  int *p = ip(buf);
  for(int64_t i = 0; i < n; i++) p[i] = (int)(i % classes);
  return 0;
}

// ── Diverse synthetic input generator ────────────────────────────────────────
// Creates N samples where sample i (with label i%classes) has:
//   pixels in band [k*band_w .. (k+1)*band_w) = 0.9
//   all other pixels = 0.1
// This gives each class a DISTINCT input pattern -> network can learn.
TZ_CPU_EXPORT int64_t tz_cpu_fill_inputs_by_class(int64_t buf, int64_t n, int64_t in_dim, int64_t classes){
  float *p = fp(buf);
  int band_w = (int)(in_dim / classes);  // pixels per class band
  for(int64_t i = 0; i < n; i++){
    int cls = (int)(i % classes);
    float *row = p + i * in_dim;
    // Background = 0.1
    for(int64_t j = 0; j < in_dim; j++) row[j] = 0.1f;
    // Class band = 0.9
    int start = cls * band_w;
    int end   = start + band_w;
    if(end > (int)in_dim) end = (int)in_dim;
    for(int j = start; j < end; j++) row[j] = 0.9f;
  }
  return 0;
}

// =============================================================================
//  NEW KERNELS v2.0 — Transformer / LLM / ARM64-ABI-safe
// =============================================================================

// ── 1. get_loss_f32 ───────────────────────────────────────────────────────────
// Returns the mean of buf[n] as int64 via bit-cast.
// Fixes Gap #3: Tezz code can now read the loss as a float programmatically.
//   float mean = *(float*)&result;  (C side)
//   In Tezz:  loss_i: int = tz_cpu_get_loss_f32(buf, n)
//             → bit-cast back in C analysis or via a reinterpret helper
TZ_CPU_EXPORT int64_t tz_cpu_get_loss_f32(int64_t buf, int64_t n){
  float *p = fp(buf);
  double sum = 0.0;
  for(int64_t i = 0; i < n; i++) sum += (double)p[i];
  float mean = (n > 0) ? (float)(sum / (double)n) : 0.0f;
  int64_t out = 0;
  memcpy(&out, &mean, 4);   // 32-bit float bits in low 32 of int64
  return out;
}

// ── 2. argmax_f32 ─────────────────────────────────────────────────────────────
// Returns index of the maximum value in x[n]. Used for greedy decoding.
TZ_CPU_EXPORT int64_t tz_cpu_argmax_f32(int64_t x, int64_t n){
  float *p = fp(x);
  int64_t best = 0;
  float   bval = p[0];
  for(int64_t i = 1; i < n; i++){
    if(p[i] > bval){ bval = p[i]; best = i; }
  }
  return best;
}

// ── 3. transpose_f32 ──────────────────────────────────────────────────────────
// out[N×M] = A[M×N]^T
TZ_CPU_EXPORT int64_t tz_cpu_transpose_f32(int64_t A, int64_t out,
                                            int64_t M, int64_t N){
  float *a = fp(A), *o = fp(out);
  for(int64_t i = 0; i < M; i++)
    for(int64_t j = 0; j < N; j++)
      o[j*M + i] = a[i*N + j];
  return 0;
}

// ── 4. bmm_f32 ────────────────────────────────────────────────────────────────
// Batched matmul: C[B,M,N] = A[B,M,K] @ B_in[B,K,N]
TZ_CPU_EXPORT int64_t tz_cpu_bmm_f32(int64_t A, int64_t B_in, int64_t C,
                                      int64_t batch, int64_t M_,
                                      int64_t K_, int64_t N_){
  float *a = fp(A), *b = fp(B_in), *c = fp(C);
  int64_t m = M_, k = K_, n = N_;
  int bi;
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for(bi = 0; bi < (int)batch; bi++){
    float *Ab = a + bi*m*k;
    float *Bb = b + bi*k*n;
    float *Cb = c + bi*m*n;
    for(int64_t i = 0; i < m; i++){
      for(int64_t j = 0; j < n; j++){
        float s = 0.0f;
        for(int64_t kk = 0; kk < k; kk++) s += Ab[i*k+kk]*Bb[kk*n+j];
        Cb[i*n+j] = s;
      }
    }
  }
  return 0;
}

// ── 5. embedding_f32 ──────────────────────────────────────────────────────────
// Token lookup: out[T,D] = table[indices[T], :] where indices are int32
TZ_CPU_EXPORT int64_t tz_cpu_embedding_f32(int64_t indices, int64_t table,
                                            int64_t out,
                                            int64_t T, int64_t D){
  int   *idx = ip(indices);
  float *tab = fp(table);
  float *o   = fp(out);
  for(int64_t t = 0; t < T; t++){
    int id = idx[t];
    float *src = tab + (int64_t)id * D;
    float *dst = o   + t * D;
    memcpy(dst, src, (size_t)D * sizeof(float));
  }
  return 0;
}

// Backward: accumulate gradient into embedding table rows
TZ_CPU_EXPORT int64_t tz_cpu_embedding_bwd_f32(int64_t grad, int64_t indices,
                                                int64_t dtable,
                                                int64_t T, int64_t D,
                                                int64_t V){
  (void)V;  // vocab size — dtable is already allocated [V,D]
  float *g   = fp(grad);
  int   *idx = ip(indices);
  float *dt  = fp(dtable);
  for(int64_t t = 0; t < T; t++){
    int id = idx[t];
    float *gsrc = g  + t * D;
    float *gdst = dt + (int64_t)id * D;
    for(int64_t d = 0; d < D; d++) gdst[d] += gsrc[d];
  }
  return 0;
}

// ── 6. rmsnorm_f32 ────────────────────────────────────────────────────────────
// RMSNorm (LLaMA-style): out[B,D] = (x[B,D] / rms(x)) * w[D]
// eps passed as int64 bit-pattern of float (ARM64-safe)
TZ_CPU_EXPORT int64_t tz_cpu_rmsnorm_f32(int64_t x, int64_t w, int64_t out,
                                          int64_t B, int64_t D,
                                          int64_t eps_bits){
  float eps; memcpy(&eps, &eps_bits, 4);
  if(eps == 0.0f) eps = 1e-6f;
  float *xp = fp(x), *wp = fp(w), *op = fp(out);
  for(int64_t b = 0; b < B; b++){
    float *xrow = xp + b*D;
    float *orow = op + b*D;
    double ss = 0.0;
    for(int64_t d = 0; d < D; d++) ss += (double)xrow[d]*(double)xrow[d];
    float scale = 1.0f / sqrtf((float)(ss/(double)D) + eps);
    for(int64_t d = 0; d < D; d++) orow[d] = xrow[d] * scale * wp[d];
  }
  return 0;
}

// RMSNorm backward: dx, dw
TZ_CPU_EXPORT int64_t tz_cpu_rmsnorm_bwd_f32(int64_t dout, int64_t x,
                                              int64_t w,
                                              int64_t dx, int64_t dw,
                                              int64_t B, int64_t D,
                                              int64_t eps_bits){
  float eps; memcpy(&eps, &eps_bits, 4);
  if(eps == 0.0f) eps = 1e-6f;
  float *do_ = fp(dout), *xp = fp(x), *wp = fp(w);
  float *dxp = fp(dx),   *dwp= fp(dw);
  if(!do_ || !xp || !wp || B <= 0 || D <= 0) return -1;
  for(int64_t b = 0; b < B; b++){
    float *xrow  = xp  + b*D;
    float *dorow = do_ + b*D;
    float *dxrow = dxp ? dxp + b*D : NULL;
    // rms = sqrt(mean(x^2) + eps)
    double ss = 0.0;
    for(int64_t d = 0; d < D; d++) ss += (double)xrow[d]*(double)xrow[d];
    float rms_inv = 1.0f / sqrtf((float)(ss/(double)D) + eps);
    // dx = (1/rms) * (dout*w - x * sum(dout*w*x) / (D*rms^2))
    double dot = 0.0;
    for(int64_t d = 0; d < D; d++) dot += (double)dorow[d]*wp[d]*xrow[d];
    float c = (float)(dot / (double)D) * rms_inv * rms_inv;
    for(int64_t d = 0; d < D; d++){
      if(dxrow) dxrow[d] += (dorow[d]*wp[d] - xrow[d]*c) * rms_inv;
      if(dwp) dwp[d] += dorow[d] * xrow[d] * rms_inv;
    }
  }
  return 0;
}

// ── 7. rope_f32 ───────────────────────────────────────────────────────────────
// Rotary Position Embedding: rotate pairs (x[2i], x[2i+1]) by theta^i * pos
// x: [T, D], out: [T, D], pos_offset: starting position index
TZ_CPU_EXPORT int64_t tz_cpu_rope_f32(int64_t x, int64_t out,
                                       int64_t T, int64_t D,
                                       int64_t pos_offset){
  float *xp = fp(x), *op = fp(out);
  int64_t half = D / 2;
  for(int64_t t = 0; t < T; t++){
    float *xrow = xp + t*D;
    float *orow = op + t*D;
    float pos = (float)(t + pos_offset);
    for(int64_t i = 0; i < half; i++){
      // theta_i = 1 / (10000^(2i/D))
      float theta = pos * powf(10000.0f, -(float)(2*i)/(float)D);
      float cos_t = cosf(theta), sin_t = sinf(theta);
      float x0 = xrow[i], x1 = xrow[i + half];
      orow[i]        = x0*cos_t - x1*sin_t;
      orow[i + half] = x0*sin_t + x1*cos_t;
    }
  }
  return 0;
}

// ── 8. attention_f32 ──────────────────────────────────────────────────────────
// Scaled dot-product attention with optional causal mask.
// Q,K,V: [B, T, D]  →  out: [B, T, D]
// is_causal=1: mask future positions (autoregressive)
// Uses a temporary score buffer allocated on heap.
TZ_CPU_EXPORT int64_t tz_cpu_attention_f32(int64_t Q, int64_t K, int64_t V,
                                            int64_t out,
                                            int64_t B, int64_t T, int64_t D,
                                            int64_t is_causal){
  float *q = fp(Q), *k = fp(K), *v = fp(V), *o = fp(out);
  float scale = 1.0f / sqrtf((float)D);
  // Allocate score matrix [T, T] per batch
  float *scores = (float*)malloc((size_t)(T*T)*sizeof(float));
  if(!scores) return -1;

  for(int64_t b = 0; b < B; b++){
    float *qb = q + b*T*D;
    float *kb = k + b*T*D;
    float *vb = v + b*T*D;
    float *ob = o + b*T*D;

    // 1. scores[i,j] = Q[i,:] · K[j,:] * scale
    for(int64_t i = 0; i < T; i++){
      for(int64_t j = 0; j < T; j++){
        float s = 0.0f;
        for(int64_t d = 0; d < D; d++) s += qb[i*D+d]*kb[j*D+d];
        scores[i*T+j] = s * scale;
        // Causal mask: future positions → -inf
        if(is_causal && j > i) scores[i*T+j] = -1e9f;
      }
    }

    // 2. Softmax over each row
    for(int64_t i = 0; i < T; i++){
      float *row = scores + i*T;
      float mx = row[0];
      for(int64_t j = 1; j < T; j++) if(row[j] > mx) mx = row[j];
      float sum = 0.0f;
      for(int64_t j = 0; j < T; j++){ row[j] = expf(row[j]-mx); sum += row[j]; }
      float inv = (sum > 0.0f) ? 1.0f/sum : 0.0f;
      for(int64_t j = 0; j < T; j++) row[j] *= inv;
    }

    // 3. out[i,:] = sum_j scores[i,j] * V[j,:]
    for(int64_t i = 0; i < T; i++){
      float *orow = ob + i*D;
      for(int64_t d = 0; d < D; d++) orow[d] = 0.0f;
      for(int64_t j = 0; j < T; j++){
        float a = scores[i*T+j];
        for(int64_t d = 0; d < D; d++) orow[d] += a * vb[j*D+d];
      }
    }
  }

  free(scores);
  return 0;
}

// ── attention_bwd_f32 ─────────────────────────────────────────────────────────
// Backward pass for scaled dot-product attention.
// dout: [B,T,D] → dQ, dK, dV: [B,T,D]  (accumulate)
// Requires the softmax scores from forward — recompute them here.
TZ_CPU_EXPORT int64_t tz_cpu_attention_bwd_f32(int64_t Q, int64_t K,
                                                int64_t V, int64_t dout,
                                                int64_t dQ, int64_t dK,
                                                int64_t dV,
                                                int64_t B, int64_t T,
                                                int64_t D,
                                                int64_t is_causal){
  float *q=fp(Q), *k=fp(K), *v=fp(V), *do_=fp(dout);
  float *dq=fp(dQ), *dk=fp(dK), *dv=fp(dV);
  float scale = 1.0f / sqrtf((float)D);
  float *scores = (float*)malloc((size_t)(T*T)*sizeof(float));
  float *dscores = (float*)malloc((size_t)(T*T)*sizeof(float));
  if(!scores || !dscores){ free(scores); free(dscores); return -1; }

  for(int64_t b = 0; b < B; b++){
    float *qb=q+b*T*D, *kb=k+b*T*D, *vb=v+b*T*D, *dob=do_+b*T*D;
    float *dqb=dq+b*T*D, *dkb=dk+b*T*D, *dvb=dv+b*T*D;

    // Recompute softmax scores
    for(int64_t i = 0; i < T; i++){
      for(int64_t j = 0; j < T; j++){
        float s = 0.0f;
        for(int64_t d = 0; d < D; d++) s += qb[i*D+d]*kb[j*D+d];
        scores[i*T+j] = s*scale;
        if(is_causal && j > i) scores[i*T+j] = -1e9f;
      }
      float *row=scores+i*T, mx=row[0];
      for(int64_t j=1;j<T;j++) if(row[j]>mx) mx=row[j];
      float sm=0.0f;
      for(int64_t j=0;j<T;j++){row[j]=expf(row[j]-mx);sm+=row[j];}
      float inv=(sm>0.f)?1.f/sm:0.f;
      for(int64_t j=0;j<T;j++) row[j]*=inv;
    }

    // dV += scores^T @ dout  (dV[j,:] += sum_i scores[i,j]*dout[i,:])
    for(int64_t j = 0; j < T; j++)
      for(int64_t d = 0; d < D; d++){
        float acc=0.0f;
        for(int64_t i=0;i<T;i++) acc += scores[i*T+j]*dob[i*D+d];
        dvb[j*D+d] += acc;
      }

    // dscores[i,j] = dout[i,:] · V[j,:]
    for(int64_t i=0;i<T;i++)
      for(int64_t j=0;j<T;j++){
        float s=0.0f;
        for(int64_t d=0;d<D;d++) s+=dob[i*D+d]*vb[j*D+d];
        dscores[i*T+j]=(is_causal&&j>i)?0.0f:s;
      }

    // Softmax backward: ds_raw[i,j] = p[i,j]*(dscores[i,j] - sum_k p[i,k]*dscores[i,k])
    for(int64_t i=0;i<T;i++){
      float dot=0.0f;
      for(int64_t j=0;j<T;j++) dot+=scores[i*T+j]*dscores[i*T+j];
      for(int64_t j=0;j<T;j++)
        dscores[i*T+j]=scores[i*T+j]*(dscores[i*T+j]-dot)*scale;
    }

    // dQ += dscores @ K
    for(int64_t i=0;i<T;i++)
      for(int64_t d=0;d<D;d++){
        float acc=0.0f;
        for(int64_t j=0;j<T;j++) acc+=dscores[i*T+j]*kb[j*D+d];
        dqb[i*D+d]+=acc;
      }
    // dK += dscores^T @ Q
    for(int64_t j=0;j<T;j++)
      for(int64_t d=0;d<D;d++){
        float acc=0.0f;
        for(int64_t i=0;i<T;i++) acc+=dscores[i*T+j]*qb[i*D+d];
        dkb[j*D+d]+=acc;
      }
  }

  free(scores); free(dscores);
  return 0;
}
