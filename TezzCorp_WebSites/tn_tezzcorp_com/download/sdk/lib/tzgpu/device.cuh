#pragma once

// =============================================================================
//  ELEMENTWISE KERNELS (f32 primary)
// =============================================================================

__global__ void k_add_f32(const float* a, const float* b, float* c, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) c[i]=a[i]+b[i];
}
__global__ void k_sub_f32(const float* a, const float* b, float* c, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) c[i]=a[i]-b[i];
}
__global__ void k_mul_f32(const float* a, const float* b, float* c, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) c[i]=a[i]*b[i];
}
__global__ void k_scale_f32(const float* a, float* c, int n, float s){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) c[i]=a[i]*s;
}
__global__ void k_fill_f32(float* a, int n, float val){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) a[i]=val;
}
__global__ void k_copy_f32(const float* src, float* dst, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) dst[i]=src[i];
}
__global__ void k_axpy_f32(float* y, const float* x, float alpha, int n){
  // y = alpha*x + y
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) y[i]+=alpha*x[i];
}
__global__ void k_square_f32(const float* a, float* c, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) c[i]=a[i]*a[i];
}
__global__ void k_sqrt_f32(const float* a, float* c, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) c[i]=sqrtf(a[i]+1e-8f);
}
__global__ void k_clamp_f32(float* a, int n, float lo, float hi){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n){ if(a[i]<lo) a[i]=lo; if(a[i]>hi) a[i]=hi; }
}

// =============================================================================
//  ACTIVATION KERNELS (f32)
// =============================================================================

__global__ void k_relu_f32(const float* a, float* c, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) c[i]=a[i]>0.f?a[i]:0.f;
}
__global__ void k_relu_bwd_f32(const float* grad_out, const float* x, float* grad_in, int n){
  // grad_in = grad_out * (x > 0)
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) grad_in[i]=x[i]>0.f?grad_out[i]:0.f;
}
__global__ void k_leaky_relu_f32(const float* a, float* c, int n, float alpha){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) c[i]=a[i]>0.f?a[i]:alpha*a[i];
}
__global__ void k_sigmoid_f32(const float* a, float* c, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) c[i]=1.f/(1.f+expf(-a[i]));
}
__global__ void k_sigmoid_bwd_f32(const float* grad_out, const float* sig_out, float* grad_in, int n){
  // grad_in = grad_out * sig*(1-sig)
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n){ float s=sig_out[i]; grad_in[i]=grad_out[i]*s*(1.f-s); }
}
__global__ void k_tanh_f32(const float* a, float* c, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) c[i]=tanhf(a[i]);
}
__global__ void k_tanh_bwd_f32(const float* grad_out, const float* tanh_out, float* grad_in, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n){ float t=tanh_out[i]; grad_in[i]=grad_out[i]*(1.f-t*t); }
}
// GELU uses the same tanh approximation as the CPU tensor engine.
__global__ void k_gelu_f32(const float* a, float* c, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n){ float x=a[i]; c[i]=0.5f*x*(1.f+tanhf(0.7978845608f*(x+0.044715f*x*x*x))); }
}
__global__ void k_gelu_bwd_f32(const float* grad_out, const float* x, float* grad_in, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n){
    float value=x[i];
    float th=tanhf(0.7978845608f*(value+0.044715f*value*value*value));
    float derivative=0.5f*(1.f+th)+0.5f*value*(1.f-th*th)*0.7978845608f*(1.f+0.134145f*value*value);
    grad_in[i] = grad_out[i]*derivative;
  }
}

// =============================================================================
//  SOFTMAX (numerically stable, row-wise)
// =============================================================================

__global__ void k_softmax_f32(float* x, int rows, int cols){
  int row = blockIdx.x*blockDim.x+threadIdx.x;
  if(row>=rows) return;
  float* r = x + row*cols;
  float mx = r[0];
  for(int j=1;j<cols;j++) if(r[j]>mx) mx=r[j];
  float s = 0.f;
  for(int j=0;j<cols;j++){ r[j]=expf(r[j]-mx); s+=r[j]; }
  for(int j=0;j<cols;j++) r[j]/=s;
}

// =============================================================================
//  BIAS ADD (add bias vector b[out_dim] to every row of x[batch×out_dim])
// =============================================================================

__global__ void k_add_bias_f32(float* x, const float* b, int batch, int dim){
  int idx = blockIdx.x*blockDim.x+threadIdx.x;
  if(idx >= batch*dim) return;
  x[idx] += b[idx % dim];
}

// =============================================================================
//  LAYER NORM (row-wise, in-place, f32)
// =============================================================================

__global__ void k_layernorm_f32(float* x, const float* gamma, const float* beta,
                                  int rows, int cols, float eps){
  int row = blockIdx.x*blockDim.x+threadIdx.x;
  if(row>=rows) return;
  float* r = x + row*cols;
  float mu=0.f;
  for(int j=0;j<cols;j++) mu+=r[j];
  mu/=cols;
  float va=0.f;
  for(int j=0;j<cols;j++){ float d=r[j]-mu; va+=d*d; }
  va/=cols;
  float inv_std=rsqrtf(va+eps);
  for(int j=0;j<cols;j++){
    float norm=(r[j]-mu)*inv_std;
    r[j] = (gamma ? gamma[j]*norm : norm) + (beta ? beta[j] : 0.f);
  }
}

// LayerNorm backward (simplified — no gamma/beta grad for now, just dx)
__global__ void k_layernorm_bwd_f32(float* dx, const float* dy, const float* x,
                                      const float* gamma, int rows, int cols, float eps){
  int row = blockIdx.x*blockDim.x+threadIdx.x;
  if(row>=rows) return;
  const float* xr = x + row*cols;
  const float* dyr = dy + row*cols;
  float* dxr = dx + row*cols;
  float mu=0.f, va=0.f;
  for(int j=0;j<cols;j++) mu+=xr[j];
  mu/=cols;
  for(int j=0;j<cols;j++){ float d=xr[j]-mu; va+=d*d; }
  va/=cols;
  float inv_std=rsqrtf(va+eps);
  float N=(float)cols;
  float s1=0.f, s2=0.f;
  for(int j=0;j<cols;j++){
    float g=gamma?gamma[j]:1.f;
    s1+=dyr[j]*g;
    s2+=dyr[j]*g*(xr[j]-mu);
  }
  for(int j=0;j<cols;j++){
    float g=gamma?gamma[j]:1.f;
    float xhat=(xr[j]-mu)*inv_std;
    dxr[j]=inv_std/N*(N*dyr[j]*g - s1 - xhat*inv_std*s2);
  }
}

// =============================================================================
//  BATCH NORM (channel-wise, inference mode: uses running mean/var)
// =============================================================================

__global__ void k_batchnorm_fwd_f32(float* x, const float* mean, const float* var,
                                      const float* gamma, const float* beta,
                                      int N, int C, int HW, float eps){
  // x layout: [N, C, HW]
  int idx = blockIdx.x*blockDim.x+threadIdx.x;
  if(idx >= N*C*HW) return;
  int c = (idx / HW) % C;
  float inv_std = rsqrtf(var[c]+eps);
  x[idx] = gamma[c]*((x[idx]-mean[c])*inv_std)+beta[c];
}

// =============================================================================
//  DROPOUT (training only, uses native counter RNG per-element)
// =============================================================================

__device__ uint64_t tz_random_bits(uint64_t seed, uint64_t counter){
  uint64_t z=seed + 0x9e3779b97f4a7c15ULL*(counter+1);
  z=(z^(z>>30))*0xbf58476d1ce4e5b9ULL;
  z=(z^(z>>27))*0x94d049bb133111ebULL;
  return z^(z>>31);
}
__device__ float tz_uniform(uint64_t seed, uint64_t counter){
  return (float)((tz_random_bits(seed,counter)>>40)+1) * (1.f/16777216.f);
}

__global__ void k_dropout_f32(float* x, float* mask, int n, float rate, unsigned long long seed){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i>=n) return;
  float r = tz_uniform(seed, (uint64_t)i);
  float keep = r > rate ? 1.f/(1.f-rate) : 0.f;
  mask[i] = keep;
  x[i] *= keep;
}

// =============================================================================
//  EMBEDDING LOOKUP
// =============================================================================

__global__ void k_embedding_f32(const int* indices, const float* table,
                                  float* out, int n, int embed_dim){
  // out[i*embed_dim .. +embed_dim] = table[indices[i]*embed_dim ..]
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i>=n) return;
  int idx = indices[i];
  const float* src = table + idx*embed_dim;
  float* dst = out + i*embed_dim;
  for(int j=0;j<embed_dim;j++) dst[j]=src[j];
}

// Embedding backward: scatter-add grad into weight table
__global__ void k_embedding_bwd_f32(const int* indices, const float* grad_out,
                                      float* grad_table, int n, int embed_dim){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i>=n) return;
  int idx = indices[i];
  const float* gsrc = grad_out + i*embed_dim;
  float* gdst = grad_table + idx*embed_dim;
  for(int j=0;j<embed_dim;j++) atomicAdd(&gdst[j], gsrc[j]);
}

// =============================================================================
//  ROTARY POSITION EMBEDDING (RoPE) — for transformer attention
// =============================================================================

__global__ void k_rope_f32(float* x, int seq, int heads, int dim_head){
  // x: [seq, heads, dim_head]
  int tid = blockIdx.x*blockDim.x+threadIdx.x;
  int total = seq*heads*(dim_head/2);
  if(tid>=total) return;
  int half = dim_head/2;
  int h_idx = tid % half;
  int head  = (tid / half) % heads;
  int pos   = tid / (half*heads);
  float theta = (float)pos / powf(10000.f, 2.f*(float)h_idx/(float)dim_head);
  float cosT = cosf(theta), sinT = sinf(theta);
  int base = (pos*heads+head)*dim_head + h_idx;
  float x0 = x[base], x1 = x[base+half];
  x[base]      = x0*cosT - x1*sinT;
  x[base+half] = x0*sinT + x1*cosT;
}

// =============================================================================
//  CAUSAL ATTENTION MASK (set upper-triangle to -1e9)
// =============================================================================

__global__ void k_causal_mask_f32(float* scores, int batch_heads, int seq){
  int idx = blockIdx.x*blockDim.x+threadIdx.x;
  if(idx >= batch_heads*seq*seq) return;
  int col = idx % seq;
  int row = (idx / seq) % seq;
  if(col > row) scores[idx] = -1e9f;
}

// =============================================================================
//  CROSS-ENTROPY LOSS (stable: logsumexp trick)
// =============================================================================

// Returns per-sample loss; sum externally
__global__ void k_cross_entropy_f32(const float* logits, const int* labels,
                                      float* loss, int batch, int classes){
  int b = blockIdx.x*blockDim.x+threadIdx.x;
  if(b>=batch) return;
  const float* l = logits + b*classes;
  float mx=l[0];
  for(int c=1;c<classes;c++) if(l[c]>mx) mx=l[c];
  float s=0.f;
  for(int c=0;c<classes;c++) s+=expf(l[c]-mx);
  loss[b] = -(l[labels[b]]-mx) + logf(s);
}

// Cross-entropy gradient: softmax(logits) - one_hot(label)
__global__ void k_cross_entropy_bwd_f32(const float* logits, const int* labels,
                                          float* grad, int batch, int classes){
  int idx = blockIdx.x*blockDim.x+threadIdx.x;
  if(idx >= batch*classes) return;
  int b = idx / classes;
  int c = idx % classes;
  const float* l = logits + b*classes;
  float mx=l[0];
  for(int cc=1;cc<classes;cc++) if(l[cc]>mx) mx=l[cc];
  float s=0.f;
  for(int cc=0;cc<classes;cc++) s+=expf(l[cc]-mx);
  float sm = expf(l[c]-mx)/s;
  grad[idx] = (sm - (c==labels[b]?1.f:0.f)) / (float)batch;
}

// =============================================================================
//  MSE LOSS
// =============================================================================

__global__ void k_mse_loss_f32(const float* pred, const float* target,
                                 float* loss, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n){ float d=pred[i]-target[i]; loss[i]=d*d; }
}
__global__ void k_mse_bwd_f32(const float* pred, const float* target,
                                float* grad, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) grad[i] = 2.f*(pred[i]-target[i])/(float)n;
}

// =============================================================================
//  ADAM OPTIMIZER (fused, in-place)
// =============================================================================

__global__ void k_adam_f32(float* p, const float* g, float* m, float* v,
                             float lr, float b1, float b2, float eps,
                             float b1t, float b2t, int n){
  // b1t = b1^t, b2t = b2^t (precomputed bias correction)
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i>=n) return;
  float gi = g[i];
  m[i] = b1*m[i] + (1.f-b1)*gi;
  v[i] = b2*v[i] + (1.f-b2)*gi*gi;
  float mhat = m[i]/(1.f-b1t);
  float vhat = v[i]/(1.f-b2t);
  p[i] -= lr * mhat / (sqrtf(vhat)+eps);
}

// =============================================================================
//  SGD WITH MOMENTUM
// =============================================================================

__global__ void k_sgd_f32(float* p, const float* g, float* vel,
                            float lr, float momentum, float wd, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i>=n) return;
  float grad = g[i] + wd*p[i];
  vel[i] = momentum*vel[i] + grad;
  p[i] -= lr * vel[i];
}

// =============================================================================
//  GRADIENT CLIPPING (clip by global L2 norm)
// =============================================================================

__global__ void k_grad_clip_f32(float* g, int n, float max_norm, float actual_norm){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i>=n) return;
  if(actual_norm > max_norm) g[i] *= max_norm/actual_norm;
}

// Reduce sum (for norm computation) — one block only, small n
__global__ void k_reduce_sum_sq_f32(const float* x, float* out, int n){
  __shared__ float sdata[BLOCK_1D];
  int tid = threadIdx.x;
  int i = blockIdx.x*blockDim.x+tid;
  sdata[tid] = (i<n) ? x[i]*x[i] : 0.f;
  __syncthreads();
  for(int s=blockDim.x/2; s>0; s>>=1){
    if(tid<s) sdata[tid]+=sdata[tid+s];
    __syncthreads();
  }
  if(tid==0) atomicAdd(out, sdata[0]);
}

// =============================================================================
//  DTYPE CONVERSION
// =============================================================================

__global__ void k_f32_to_f64(const float* src, double* dst, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) dst[i]=(double)src[i];
}
__global__ void k_f64_to_f32(const double* src, float* dst, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) dst[i]=(float)src[i];
}

// =============================================================================
//  TILED MATMUL (native f64 device implementation, compatible with v1)
// =============================================================================

__global__ void k_matmul_f64(const double* a, const double* b, double* c,
                               int M, int K, int N, double alpha, double beta){
  __shared__ double As[TILE_DIM][TILE_DIM];
  __shared__ double Bs[TILE_DIM][TILE_DIM];
  int row=blockIdx.y*TILE_DIM+threadIdx.y;
  int col=blockIdx.x*TILE_DIM+threadIdx.x;
  double acc=0.0;
  for(int t=0; t<K/TILE_DIM+(K%TILE_DIM!=0); t++){
    As[threadIdx.y][threadIdx.x] = (row<M && t*TILE_DIM+threadIdx.x<K) ?
      a[row*K + t*TILE_DIM+threadIdx.x] : 0.0;
    Bs[threadIdx.y][threadIdx.x] = (col<N && t*TILE_DIM+threadIdx.y<K) ?
      b[(t*TILE_DIM+threadIdx.y)*N + col] : 0.0;
    __syncthreads();
    for(int k=0;k<TILE_DIM;k++) acc+=As[threadIdx.y][k]*Bs[k][threadIdx.x];
    __syncthreads();
  }
  if(row<M && col<N) c[row*N+col]=alpha*acc+(beta==0.0 ? 0.0 : beta*c[row*N+col]);
}

// f64 elementwise (kept for v1 compatibility)
__global__ void k_add_f64(const double* a, const double* b, double* c, int n){
  int i=blockIdx.x*blockDim.x+threadIdx.x; if(i<n) c[i]=a[i]+b[i];
}
__global__ void k_mul_f64(const double* a, const double* b, double* c, int n){
  int i=blockIdx.x*blockDim.x+threadIdx.x; if(i<n) c[i]=a[i]*b[i];
}
__global__ void k_relu_f64(const double* a, double* c, int n){
  int i=blockIdx.x*blockDim.x+threadIdx.x; if(i<n) c[i]=a[i]>0.0?a[i]:0.0;
}
__global__ void k_scale_f64(const double* a, double* c, int n, double s){
  int i=blockIdx.x*blockDim.x+threadIdx.x; if(i<n) c[i]=a[i]*s;
}
__global__ void k_gelu_f64(const double* a, double* c, int n){
  int i=blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n){ double x=a[i]; c[i]=0.5*x*(1.0+tanh(0.7978845608028654*(x+0.044715*x*x*x))); }
}
__global__ void k_sigmoid_f64(const double* a, double* c, int n){
  int i=blockIdx.x*blockDim.x+threadIdx.x; if(i<n) c[i]=1.0/(1.0+exp(-a[i]));
}
__global__ void k_softmax_f64(double* x, int rows, int cols){
  int row=blockIdx.x*blockDim.x+threadIdx.x; if(row>=rows) return;
  double* r=x+row*cols; double mx=r[0];
  for(int j=1;j<cols;j++) if(r[j]>mx) mx=r[j];
  double s=0.0;
  for(int j=0;j<cols;j++){ r[j]=exp(r[j]-mx); s+=r[j]; }
  for(int j=0;j<cols;j++) r[j]/=s;
}
__global__ void k_layer_norm_f64(double* x, int rows, int cols, double eps){
  int row=blockIdx.x*blockDim.x+threadIdx.x; if(row>=rows) return;
  double* rp=x+row*cols;
  double mu=0.0; for(int j=0;j<cols;j++) mu+=rp[j]; mu/=cols;
  double va=0.0; for(int j=0;j<cols;j++){ double d=rp[j]-mu; va+=d*d; } va/=cols;
  double inv_std=1.0/sqrt(va+eps);
  for(int j=0;j<cols;j++) rp[j]=(rp[j]-mu)*inv_std;
}

// ── Column-wise reduce (bias gradient: db = sum_rows(dY)) ─────────────────────
// out[j] += sum_i X[i, j]   for i in [0, rows), j in [0, cols)
__global__ void k_reduce_sum_cols_f32(const float* X, float* out, int rows, int cols){
  int j = blockIdx.x * blockDim.x + threadIdx.x;
  if(j >= cols) return;
  float s = 0.f;
  for(int i = 0; i < rows; i++) s += X[i * cols + j];
  atomicAdd(&out[j], s);
}

// ── Xavier / He init via native counter RNG ───────────────────────────────────────────────
// Fills W[n] with N(0, std) where std = sqrt(2.0 / fan_in)  (He initialization)
__global__ void k_xavier_init_f32(float* W, int n, float std, unsigned long long seed){
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if(i >= n) return;
  float u=tz_uniform(seed,(uint64_t)i*2);
  float v=tz_uniform(seed,(uint64_t)i*2+1);
  W[i] = sqrtf(-2.f*logf(u))*cosf(6.283185307179586f*v)*std;
}


// Row-major tiled GEMM, including transposed storage, entirely on the GPU.
__global__ void k_gemm_f32(const float* a, const float* b, float* c,
                          int M, int K, int N, float alpha, float beta,
                          int trans_a, int trans_b){
  __shared__ float as[TILE_DIM][TILE_DIM], bs[TILE_DIM][TILE_DIM];
  int row=blockIdx.y*TILE_DIM+threadIdx.y, col=blockIdx.x*TILE_DIM+threadIdx.x;
  float acc=0.f;
  for(int tile=0; tile<K/TILE_DIM+(K%TILE_DIM!=0); tile++){
    int ak=tile*TILE_DIM+threadIdx.x, bk=tile*TILE_DIM+threadIdx.y;
    as[threadIdx.y][threadIdx.x]=(row<M && ak<K) ? a[trans_a ? ak*M+row : row*K+ak] : 0.f;
    bs[threadIdx.y][threadIdx.x]=(col<N && bk<K) ? b[trans_b ? col*K+bk : bk*N+col] : 0.f;
    __syncthreads();
    for(int k=0;k<TILE_DIM;k++) acc+=as[threadIdx.y][k]*bs[k][threadIdx.x];
    __syncthreads();
  }
  if(row<M && col<N) c[row*N+col]=alpha*acc+(beta==0.f ? 0.f : beta*c[row*N+col]);
}

__global__ void k_check_indices(const int* values, int count, int upper, int* invalid){
  int i=blockIdx.x*blockDim.x+threadIdx.x;
  if(i<count && (values[i]<0 || values[i]>=upper)) atomicExch(invalid,1);
}
