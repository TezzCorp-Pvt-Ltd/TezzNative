// TezzNative native CUDA kernels. Runtime dependency: CUDA cudart only.
// Linux: nvcc -O2 -arch=sm_75 -shared -Xcompiler -fPIC --cudart=shared tzgpu_kernels.cu -o libtzgpu.so
// Windows: use the supported MSVC host toolchain and select the target SM explicitly.
// GEMM uses our tiled device implementation; cuBLAS performance parity is not claimed.

#include <cuda_runtime.h>
#include <cuda.h>
#include <limits.h>
#include <cuda_fp16.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

#if defined(TN_NATIVE_X64_ABI)
#  include "../include/native_abi.h"
#  define TZ_EXPORT TN_NATIVE_API
#elif defined(_WIN32)
#  define TZ_EXPORT __declspec(dllexport)
#else
#  define TZ_EXPORT
#endif

// ─── Thread geometry ───────────────────────────────────────────────────────────
#define BLOCK_1D   256
#define TILE_DIM    16

#include "tzgpu/device.cuh"
#include "tzgpu/validation.cuh"
#include "tzgpu/temporal.cuh"

// =============================================================================
//  EXPORTED C API — ALL FUNCTIONS VISIBLE TO tzgpu.dll IMPORT TABLE
// =============================================================================
extern "C" {

// ── Memory ────────────────────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_cuda_backend_kind(void){ return 1; }

TZ_EXPORT int64_t tz_cuda_malloc(int64_t nbytes){
  if(nbytes<=0) return 0;
  void* p=NULL; if(cudaMalloc(&p,(size_t)nbytes)!=cudaSuccess) return 0; return (int64_t)p;
}
TZ_EXPORT int64_t tz_cuda_free(int64_t ptr){
  return cudaFree((void*)ptr)==cudaSuccess ? 0 : -1;
}
TZ_EXPORT int64_t tz_cuda_memcpy(int64_t dst, int64_t src, int64_t n, int64_t kind){
  if(n<0 || kind<0 || kind>3) return -1;
  if(n==0) return 0;
  if(!dst || !src) return -1;
  if((kind==1 || kind==3) && (uint64_t)n>tz_available(dst)) return -1;
  if((kind==2 || kind==3) && (uint64_t)n>tz_available(src)) return -1;
  return cudaMemcpy((void*)dst,(void*)src,(size_t)n,(cudaMemcpyKind)kind)==cudaSuccess ? 0 : -1;
}
TZ_EXPORT int64_t tz_cuda_sync(){ return cudaDeviceSynchronize()==cudaSuccess ? 0 : -1; }
TZ_EXPORT int64_t tz_cuda_device_count(){ int n=0; return cudaGetDeviceCount(&n)==cudaSuccess ? n : 0; }
TZ_EXPORT int64_t tz_cuda_set_device(int64_t idx){
  if(idx<0 || idx>INT_MAX) return -1;
  return cudaSetDevice((int)idx)==cudaSuccess ? 0 : -1;
}
TZ_EXPORT int64_t tz_cuda_mem_free(){ size_t f=0,t=0; return cudaMemGetInfo(&f,&t)==cudaSuccess ? (int64_t)f : -1; }
TZ_EXPORT int64_t tz_cuda_mem_total(){ size_t f=0,t=0; return cudaMemGetInfo(&f,&t)==cudaSuccess ? (int64_t)t : -1; }

TZ_EXPORT int64_t tz_gpu_add_f32(int64_t a, int64_t b, int64_t c, int64_t n){
  if(!(tz_span(a,n) && tz_span(b,n) && tz_span(c,n))) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_add_f32<<<bl,BLOCK_1D>>>((float*)a,(float*)b,(float*)c,(int)n); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_sub_f32(int64_t a, int64_t b, int64_t c, int64_t n){
  if(!(tz_span(a,n) && tz_span(b,n) && tz_span(c,n))) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_sub_f32<<<bl,BLOCK_1D>>>((float*)a,(float*)b,(float*)c,(int)n); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_mul_f32(int64_t a, int64_t b, int64_t c, int64_t n){
  if(!(tz_span(a,n) && tz_span(b,n) && tz_span(c,n))) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_mul_f32<<<bl,BLOCK_1D>>>((float*)a,(float*)b,(float*)c,(int)n); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_scale_f32(int64_t a, int64_t c, int64_t n, double s){
  if(!(tz_span(a,n) && tz_span(c,n))) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_scale_f32<<<bl,BLOCK_1D>>>((float*)a,(float*)c,(int)n,(float)s); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_fill_f32(int64_t a, int64_t n, double val){
  if(!(tz_span(a,n))) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_fill_f32<<<bl,BLOCK_1D>>>((float*)a,(int)n,(float)val); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_copy_f32(int64_t src, int64_t dst, int64_t n){
  if(!(tz_span(src,n) && tz_span(dst,n))) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_copy_f32<<<bl,BLOCK_1D>>>((float*)src,(float*)dst,(int)n); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_axpy_f32(int64_t y, int64_t x, double alpha, int64_t n){
  if(!(tz_span(y,n) && tz_span(x,n))) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_axpy_f32<<<bl,BLOCK_1D>>>((float*)y,(float*)x,(float)alpha,(int)n); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_clamp_f32(int64_t a, int64_t n, double lo, double hi){
  if(!(tz_span(a,n) && isfinite(lo) && isfinite(hi) && lo<=hi)) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_clamp_f32<<<bl,BLOCK_1D>>>((float*)a,(int)n,(float)lo,(float)hi); return tz_launch_status();
}

// ── f32 Activations ───────────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_relu_f32(int64_t a, int64_t c, int64_t n){
  if(!(tz_span(a,n) && tz_span(c,n))) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_relu_f32<<<bl,BLOCK_1D>>>((float*)a,(float*)c,(int)n); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_relu_bwd_f32(int64_t grad_out, int64_t x, int64_t grad_in, int64_t n){
  if(!(tz_span(grad_out,n) && tz_span(x,n) && tz_span(grad_in,n))) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_relu_bwd_f32<<<bl,BLOCK_1D>>>((float*)grad_out,(float*)x,(float*)grad_in,(int)n); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_leaky_relu_f32(int64_t a, int64_t c, int64_t n, double alpha){
  if(!(tz_span(a,n) && tz_span(c,n))) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_leaky_relu_f32<<<bl,BLOCK_1D>>>((float*)a,(float*)c,(int)n,(float)alpha); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_sigmoid_f32(int64_t a, int64_t c, int64_t n){
  if(!(tz_span(a,n) && tz_span(c,n))) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_sigmoid_f32<<<bl,BLOCK_1D>>>((float*)a,(float*)c,(int)n); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_sigmoid_bwd_f32(int64_t grad_out, int64_t sig_out, int64_t grad_in, int64_t n){
  if(!(tz_span(grad_out,n) && tz_span(sig_out,n) && tz_span(grad_in,n))) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_sigmoid_bwd_f32<<<bl,BLOCK_1D>>>((float*)grad_out,(float*)sig_out,(float*)grad_in,(int)n); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_tanh_f32(int64_t a, int64_t c, int64_t n){
  if(!(tz_span(a,n) && tz_span(c,n))) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_tanh_f32<<<bl,BLOCK_1D>>>((float*)a,(float*)c,(int)n); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_tanh_bwd_f32(int64_t grad_out, int64_t tanh_out, int64_t grad_in, int64_t n){
  if(!(tz_span(grad_out,n) && tz_span(tanh_out,n) && tz_span(grad_in,n))) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_tanh_bwd_f32<<<bl,BLOCK_1D>>>((float*)grad_out,(float*)tanh_out,(float*)grad_in,(int)n); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_gelu_f32(int64_t a, int64_t c, int64_t n){
  if(!(tz_span(a,n) && tz_span(c,n))) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_gelu_f32<<<bl,BLOCK_1D>>>((float*)a,(float*)c,(int)n); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_gelu_bwd_f32(int64_t grad_out, int64_t x, int64_t grad_in, int64_t n){
  if(!(tz_span(grad_out,n) && tz_span(x,n) && tz_span(grad_in,n))) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_gelu_bwd_f32<<<bl,BLOCK_1D>>>((float*)grad_out,(float*)x,(float*)grad_in,(int)n); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_softmax_f32(int64_t x, int64_t rows, int64_t cols){
  if(!(tz_span(x,tz_count2(rows,cols)))) return -1;
  int bl=(int)((rows+BLOCK_1D-1)/BLOCK_1D);
  k_softmax_f32<<<bl,BLOCK_1D>>>((float*)x,(int)rows,(int)cols); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_add_bias_f32(int64_t x, int64_t b, int64_t batch, int64_t dim){
  if(!(tz_span(x,tz_count2(batch,dim)) && tz_span(b,dim))) return -1;
  int bl=(int)((batch*dim+BLOCK_1D-1)/BLOCK_1D);
  k_add_bias_f32<<<bl,BLOCK_1D>>>((float*)x,(float*)b,(int)batch,(int)dim); return tz_launch_status();
}

// ── Layer Norm ────────────────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_layernorm_f32(int64_t x, int64_t gamma, int64_t beta,
                                        int64_t rows, int64_t cols, double eps){
  if(!(tz_span(x,tz_count2(rows,cols)) && (!gamma || tz_span(gamma,cols)) && (!beta || tz_span(beta,cols)) && isfinite(eps) && (float)eps>0)) return -1;
  int bl=(int)((rows+BLOCK_1D-1)/BLOCK_1D);
  k_layernorm_f32<<<bl,BLOCK_1D>>>((float*)x,(float*)gamma,(float*)beta,
                                    (int)rows,(int)cols,(float)eps); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_layernorm_bwd_f32(int64_t dx, int64_t dy, int64_t x,
                                             int64_t gamma, int64_t rows, int64_t cols, double eps){
  if(!(tz_span(x,tz_count2(rows,cols)) && tz_span(dx,tz_count2(rows,cols)) && tz_span(dy,tz_count2(rows,cols)) && (!gamma || tz_span(gamma,cols)) && isfinite(eps) && (float)eps>0)) return -1;
  int bl=(int)((rows+BLOCK_1D-1)/BLOCK_1D);
  k_layernorm_bwd_f32<<<bl,BLOCK_1D>>>((float*)dx,(float*)dy,(float*)x,
                                        (float*)gamma,(int)rows,(int)cols,(float)eps); return tz_launch_status();
}

// ── Batch Norm (inference) ────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_batchnorm_f32(int64_t x, int64_t mean, int64_t var,
                                         int64_t gamma, int64_t beta,
                                         int64_t N, int64_t C, int64_t HW, double eps){
  if(!(tz_span(x,tz_count2(tz_count2(N,C),HW)) && tz_span(mean,C) && tz_span(var,C) && tz_span(gamma,C) && tz_span(beta,C) && isfinite(eps) && (float)eps>0)) return -1;
  int bl=(int)((N*C*HW+BLOCK_1D-1)/BLOCK_1D);
  k_batchnorm_fwd_f32<<<bl,BLOCK_1D>>>((float*)x,(float*)mean,(float*)var,
                                        (float*)gamma,(float*)beta,
                                        (int)N,(int)C,(int)HW,(float)eps); return tz_launch_status();
}

// ── Dropout ───────────────────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_dropout_f32(int64_t x, int64_t mask, int64_t n, double rate, int64_t seed){
  if(!(tz_span(x,n) && tz_span(mask,n) && !tz_overlap(x,n*4,mask,n*4) && rate>=0 && (float)rate<1)) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_dropout_f32<<<bl,BLOCK_1D>>>((float*)x,(float*)mask,(int)n,(float)rate,
                                  (unsigned long long)seed); return tz_launch_status();
}

// ── Embedding ─────────────────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_embedding_f32(int64_t indices, int64_t table, int64_t out,
                                         int64_t n, int64_t embed_dim){
  if(!(tz_span(table,embed_dim) && tz_span(out,tz_count2(n,embed_dim)) && tz_indices(indices,n,tz_available(table)/(4*embed_dim)))) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_embedding_f32<<<bl,BLOCK_1D>>>((int*)indices,(float*)table,(float*)out,(int)n,(int)embed_dim);
  return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_embedding_bwd_f32(int64_t indices, int64_t grad_out,
                                             int64_t grad_table, int64_t n, int64_t embed_dim){
  if(!(tz_span(grad_table,embed_dim) && tz_span(grad_out,tz_count2(n,embed_dim)) && tz_indices(indices,n,tz_available(grad_table)/(4*embed_dim)))) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_embedding_bwd_f32<<<bl,BLOCK_1D>>>((int*)indices,(float*)grad_out,(float*)grad_table,(int)n,(int)embed_dim);
  return tz_launch_status();
}

// ── Transformer ───────────────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_rope_f32(int64_t x, int64_t seq, int64_t heads, int64_t dim_head){
  if(!(dim_head%2==0 && tz_span(x,tz_count2(tz_count2(seq,heads),dim_head)))) return -1;
  int total=(int)(seq*heads*(dim_head/2));
  int bl=(int)(((int64_t)total+BLOCK_1D-1)/BLOCK_1D);
  k_rope_f32<<<bl,BLOCK_1D>>>((float*)x,(int)seq,(int)heads,(int)dim_head); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_causal_mask_f32(int64_t scores, int64_t batch_heads, int64_t seq){
  if(!(tz_span(scores,tz_count2(batch_heads,tz_count2(seq,seq))))) return -1;
  int bl=(int)((batch_heads*seq*seq+BLOCK_1D-1)/BLOCK_1D);
  k_causal_mask_f32<<<bl,BLOCK_1D>>>((float*)scores,(int)batch_heads,(int)seq); return tz_launch_status();
}

// ── Loss ──────────────────────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_cross_entropy_f32(int64_t logits, int64_t labels,
                                             int64_t loss_buf, int64_t batch, int64_t classes){
  if(!(tz_span(logits,tz_count2(batch,classes)) && tz_span(loss_buf,batch) && tz_indices(labels,batch,classes))) return -1;
  int bl=(int)((batch+BLOCK_1D-1)/BLOCK_1D);
  k_cross_entropy_f32<<<bl,BLOCK_1D>>>((float*)logits,(int*)labels,(float*)loss_buf,(int)batch,(int)classes);
  return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_cross_entropy_bwd_f32(int64_t logits, int64_t labels,
                                                 int64_t grad, int64_t batch, int64_t classes){
  if(!(tz_span(logits,tz_count2(batch,classes)) && tz_span(grad,tz_count2(batch,classes)) && tz_indices(labels,batch,classes))) return -1;
  int bl=(int)((batch*classes+BLOCK_1D-1)/BLOCK_1D);
  k_cross_entropy_bwd_f32<<<bl,BLOCK_1D>>>((float*)logits,(int*)labels,(float*)grad,(int)batch,(int)classes);
  return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_mse_loss_f32(int64_t pred, int64_t target, int64_t loss_buf, int64_t n){
  if(!(tz_span(pred,n) && tz_span(target,n) && tz_span(loss_buf,n))) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_mse_loss_f32<<<bl,BLOCK_1D>>>((float*)pred,(float*)target,(float*)loss_buf,(int)n); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_mse_bwd_f32(int64_t pred, int64_t target, int64_t grad, int64_t n){
  if(!(tz_span(pred,n) && tz_span(target,n) && tz_span(grad,n))) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_mse_bwd_f32<<<bl,BLOCK_1D>>>((float*)pred,(float*)target,(float*)grad,(int)n); return tz_launch_status();
}

// ── Optimizers ────────────────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_adam_f32(int64_t p, int64_t g, int64_t m, int64_t v,
                                    double lr, double b1, double b2, double eps,
                                    double b1t, double b2t, int64_t n){
  if(!(tz_span(p,n) && tz_span(g,n) && tz_span(m,n) && tz_span(v,n) && isfinite(lr) && lr>=0 && b1>=0 && (float)b1<1 && b2>=0 && (float)b2<1 && b1t>=0 && (float)b1t<1 && b2t>=0 && (float)b2t<1 && isfinite(eps) && (float)eps>0)) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_adam_f32<<<bl,BLOCK_1D>>>((float*)p,(float*)g,(float*)m,(float*)v,
                               (float)lr,(float)b1,(float)b2,(float)eps,
                               (float)b1t,(float)b2t,(int)n); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_sgd_f32(int64_t p, int64_t g, int64_t vel,
                                   double lr, double momentum, double wd, int64_t n){
  if(!(tz_span(p,n) && tz_span(g,n) && tz_span(vel,n) && isfinite(lr) && isfinite(momentum) && isfinite(wd) && lr>=0)) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_sgd_f32<<<bl,BLOCK_1D>>>((float*)p,(float*)g,(float*)vel,(float)lr,(float)momentum,(float)wd,(int)n);
  return tz_launch_status();
}

// ── Gradient utilities ────────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_reduce_sum_sq_f32(int64_t x, int64_t out_buf, int64_t n){
  if(!(tz_span(x,n) && tz_span(out_buf,1) && !tz_overlap(x,n*4,out_buf,4))) return -1;
  // out_buf must be pre-zeroed (device scalar)
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_reduce_sum_sq_f32<<<bl,BLOCK_1D>>>((float*)x,(float*)out_buf,(int)n); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_grad_clip_f32(int64_t g, int64_t n, double max_norm, double actual_norm){
  if(!(tz_span(g,n) && isfinite(max_norm) && isfinite(actual_norm) && max_norm>=0 && actual_norm>=0)) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_grad_clip_f32<<<bl,BLOCK_1D>>>((float*)g,(int)n,(float)max_norm,(float)actual_norm); return tz_launch_status();
}

// ── dtype conversion ──────────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_f32_to_f64(int64_t src, int64_t dst, int64_t n){
  if(!(tz_span(src,n) && tz_span(dst,n,8) && !tz_overlap(src,n*4,dst,n*8))) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_f32_to_f64<<<bl,BLOCK_1D>>>((float*)src,(double*)dst,(int)n); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_f64_to_f32(int64_t src, int64_t dst, int64_t n){
  if(!(tz_span(src,n,8) && tz_span(dst,n) && !tz_overlap(src,n*8,dst,n*4))) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_f64_to_f32<<<bl,BLOCK_1D>>>((double*)src,(float*)dst,(int)n); return tz_launch_status();
}

// ── f32 GEMM via native tiled device kernel ───────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_gemm_f32(int64_t a, int64_t b, int64_t c,
                                    int64_t M, int64_t K, int64_t N,
                                    double alpha, double beta){
  if(!tz_gemm_ready(a,b,c,M,K,N,4) || !isfinite(alpha) || !isfinite(beta)) return -1;
  dim3 block(TILE_DIM,TILE_DIM), grid((N+TILE_DIM-1)/TILE_DIM,(M+TILE_DIM-1)/TILE_DIM);
  k_gemm_f32<<<grid,block>>>((float*)a,(float*)b,(float*)c,(int)M,(int)K,(int)N,(float)alpha,(float)beta,0,0);
  return tz_launch_status();
}

// ── f64 legacy ops (v1 compatibility) ────────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_add(int64_t a, int64_t b, int64_t c, int64_t n){
  if(!(tz_span(a,n,8) && tz_span(b,n,8) && tz_span(c,n,8))) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_add_f64<<<bl,BLOCK_1D>>>((double*)a,(double*)b,(double*)c,(int)n); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_mul(int64_t a, int64_t b, int64_t c, int64_t n){
  if(!(tz_span(a,n,8) && tz_span(b,n,8) && tz_span(c,n,8))) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_mul_f64<<<bl,BLOCK_1D>>>((double*)a,(double*)b,(double*)c,(int)n); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_relu(int64_t a, int64_t c, int64_t n){
  if(!(tz_span(a,n,8) && tz_span(c,n,8))) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_relu_f64<<<bl,BLOCK_1D>>>((double*)a,(double*)c,(int)n); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_scale(int64_t a, int64_t c, int64_t n, double s){
  if(!(tz_span(a,n,8) && tz_span(c,n,8))) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_scale_f64<<<bl,BLOCK_1D>>>((double*)a,(double*)c,(int)n,s); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_gelu(int64_t a, int64_t c, int64_t n){
  if(!(tz_span(a,n,8) && tz_span(c,n,8))) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_gelu_f64<<<bl,BLOCK_1D>>>((double*)a,(double*)c,(int)n); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_sigmoid(int64_t a, int64_t c, int64_t n){
  if(!(tz_span(a,n,8) && tz_span(c,n,8))) return -1;
  int bl=(int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_sigmoid_f64<<<bl,BLOCK_1D>>>((double*)a,(double*)c,(int)n); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_gemm_f64(int64_t a, int64_t b, int64_t c,
                                    int64_t M, int64_t K, int64_t N,
                                    double alpha, double beta){
  if(!tz_gemm_ready(a,b,c,M,K,N,8) || !isfinite(alpha) || !isfinite(beta)) return -1;
  dim3 block(TILE_DIM,TILE_DIM), grid((N+TILE_DIM-1)/TILE_DIM,(M+TILE_DIM-1)/TILE_DIM);
  k_matmul_f64<<<grid,block>>>((double*)a,(double*)b,(double*)c,(int)M,(int)K,(int)N,alpha,beta);
  return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_matmul_tiled(int64_t a, int64_t b, int64_t c,
                                        int64_t M, int64_t K, int64_t N){
  if(!(tz_gemm_ready(a,b,c,M,K,N,8))) return -1;
  dim3 block(TILE_DIM,TILE_DIM);
  dim3 grid((N+TILE_DIM-1)/TILE_DIM,(M+TILE_DIM-1)/TILE_DIM);
  k_matmul_f64<<<grid,block>>>((double*)a,(double*)b,(double*)c,(int)M,(int)K,(int)N,1.0,0.0); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_softmax(int64_t x, int64_t rows, int64_t cols){
  if(!(tz_span(x,tz_count2(rows,cols),8))) return -1;
  int bl=(int)((rows+BLOCK_1D-1)/BLOCK_1D);
  k_softmax_f64<<<bl,BLOCK_1D>>>((double*)x,(int)rows,(int)cols); return tz_launch_status();
}
TZ_EXPORT int64_t tz_gpu_layer_norm(int64_t x, int64_t rows, int64_t cols, double eps){
  if(!(tz_span(x,tz_count2(rows,cols),8) && isfinite(eps) && eps>0)) return -1;
  int bl=(int)((rows+BLOCK_1D-1)/BLOCK_1D);
  k_layer_norm_f64<<<bl,BLOCK_1D>>>((double*)x,(int)rows,(int)cols,eps); return tz_launch_status();
}

// ── Device info ───────────────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_info(){
  int n=0; if(cudaGetDeviceCount(&n)!=cudaSuccess) return -1;
  for(int i=0;i<n;i++){
    cudaDeviceProp p; if(cudaGetDeviceProperties(&p,i)!=cudaSuccess) return -1;
    printf("[GPU %d] %s | SM %d.%d | %.1f GB VRAM\n",
      i, p.name, p.major, p.minor,
      (double)p.totalGlobalMem/(1024.0*1024.0*1024.0));
  }
  return n;
}

// =============================================================================
//  PHASE 5: BACKWARD PASS KERNELS (v2.1)
// =============================================================================

// ── Exported backward pass API ────────────────────────────────────────────────

TZ_EXPORT int64_t tz_gpu_gemm_nt_f32(int64_t A, int64_t B, int64_t C,
                                       int64_t M, int64_t K, int64_t N,
                                       double alpha, double beta){
  if(!tz_gemm_ready(A,B,C,M,K,N,4) || !isfinite(alpha) || !isfinite(beta)) return -1;
  dim3 block(TILE_DIM,TILE_DIM), grid((N+TILE_DIM-1)/TILE_DIM,(M+TILE_DIM-1)/TILE_DIM);
  k_gemm_f32<<<grid,block>>>((float*)A,(float*)B,(float*)C,(int)M,(int)K,(int)N,(float)alpha,(float)beta,0,1);
  return tz_launch_status();
}

// C[M x N] = A[K x M]^T @ B[K x N]
TZ_EXPORT int64_t tz_gpu_gemm_tn_f32(int64_t A, int64_t B, int64_t C,
                                       int64_t M, int64_t K, int64_t N,
                                       double alpha, double beta){
  if(!tz_gemm_ready(A,B,C,M,K,N,4) || !isfinite(alpha) || !isfinite(beta)) return -1;
  dim3 block(TILE_DIM,TILE_DIM), grid((N+TILE_DIM-1)/TILE_DIM,(M+TILE_DIM-1)/TILE_DIM);
  k_gemm_f32<<<grid,block>>>((float*)A,(float*)B,(float*)C,(int)M,(int)K,(int)N,(float)alpha,(float)beta,1,0);
  return tz_launch_status();
}

// Column-wise sum: out[j] = sum_i X[i*cols + j]  (bias gradient accumulation)
// out must be pre-zeroed; this kernel atomicAdds into it.
TZ_EXPORT int64_t tz_gpu_reduce_sum_cols_f32(int64_t X, int64_t out, int64_t rows, int64_t cols){
  if(!(tz_span(X,tz_count2(rows,cols)) && tz_span(out,cols) && !tz_overlap(X,rows*cols*4,out,cols*4))) return -1;
  int bl = (int)((cols+BLOCK_1D-1)/BLOCK_1D);
  k_reduce_sum_cols_f32<<<bl, BLOCK_1D>>>((float*)X, (float*)out, (int)rows, (int)cols);
  return tz_launch_status();
}

// Xavier / He weight initialization
// std = sqrt(2.0 / fan_in) for ReLU networks (He init)
TZ_EXPORT int64_t tz_gpu_xavier_init_f32(int64_t W, int64_t n, int64_t fan_in, int64_t seed){
  if(!(tz_span(W,n) && fan_in>0)) return -1;
  float std = sqrtf(2.0f / (float)fan_in);
  int bl = (int)((n+BLOCK_1D-1)/BLOCK_1D);
  k_xavier_init_f32<<<bl, BLOCK_1D>>>((float*)W, (int)n, std, (unsigned long long)seed);
  return tz_launch_status();
}

// Time-major centered Conv1D; see temporal_shape.h for layout/error contract.
TZ_EXPORT int64_t tz_gpu_conv1d_f32(int64_t x,int64_t w,int64_t bias,int64_t out,
  int64_t B,int64_t T,int64_t Cin,int64_t Cout,int64_t K,int64_t stride,int64_t dilation){
  TzTemporalShape q;
  if(!tz_temporal_shape(&q,B,T,Cin,Cout,K,stride,dilation) ||
     !tz_span(x,q.nx) || !tz_span(w,q.nw) || !tz_span(out,q.ny) || (bias && !tz_span(bias,q.O))) return -1;
  const int64_t inputs[3]={x,w,bias},sizes[3]={q.nx,q.nw,q.O};
  if(!tz_temporal_outputs(inputs,sizes,3,&out,&q.ny,1) || !tz_temporal_finite_gpu(inputs,sizes,3)) return -1;
  k_conv1d_f32<<<(q.ny+BLOCK_1D-1)/BLOCK_1D,BLOCK_1D>>>((float*)x,(float*)w,(float*)bias,(float*)out,q);
  if(tz_launch_status()!=0) return -1;
  return tz_temporal_finite_gpu(&out,&q.ny,1) ? 0 : -1;
}

TZ_EXPORT int64_t tz_gpu_conv1d_bwd_f32(int64_t dy,int64_t x,int64_t w,
  int64_t dx,int64_t dw,int64_t db,int64_t B,int64_t T,int64_t Cin,int64_t Cout,
  int64_t K,int64_t stride,int64_t dilation){
  TzTemporalShape q;
  if(!tz_temporal_shape(&q,B,T,Cin,Cout,K,stride,dilation) || (!dx && !dw && !db) ||
     !tz_span(x,q.nx) || !tz_span(w,q.nw) || !tz_span(dy,q.ny) ||
     (dx && !tz_span(dx,q.nx)) || (dw && !tz_span(dw,q.nw)) || (db && !tz_span(db,q.O))) return -1;
  const int64_t inputs[3]={x,w,dy},ni[3]={q.nx,q.nw,q.ny};
  const int64_t outputs[3]={dx,dw,db},no[3]={q.nx,q.nw,q.O};
  if(!tz_temporal_outputs(inputs,ni,3,outputs,no,3) ||
     !tz_temporal_finite_gpu(inputs,ni,3) || !tz_temporal_finite_gpu(outputs,no,3)) return -1;
  if(dx){
    k_conv1d_dx<<<(q.nx+BLOCK_1D-1)/BLOCK_1D,BLOCK_1D>>>((float*)dy,(float*)w,(float*)dx,q);
    if(tz_launch_status()!=0) return -1;
  }
  if(dw){
    k_conv1d_dw<<<(q.nw+BLOCK_1D-1)/BLOCK_1D,BLOCK_1D>>>((float*)dy,(float*)x,(float*)dw,q);
    if(tz_launch_status()!=0) return -1;
  }
  if(db){
    k_conv1d_db<<<((int64_t)q.O+BLOCK_1D-1)/BLOCK_1D,BLOCK_1D>>>((float*)dy,(float*)db,q);
    if(tz_launch_status()!=0) return -1;
  }
  return tz_temporal_finite_gpu(outputs,no,3) ? 0 : -1;
}

} // extern "C"
