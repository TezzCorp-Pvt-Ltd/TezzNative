/* TEST ONLY. Host-memory oracle for checking .tn training algorithms.
 * Never ship or select this library as a production CUDA backend.
 * The actual CPU kernels provide arithmetic; this file adapts CUDA API shapes.
 */
#include "tzcpu_kernels.c"
#if defined(TN_NATIVE_X64_ABI)
#define ORACLE_EXPORT TN_NATIVE_API
#elif defined(_WIN32)
#define ORACLE_EXPORT __declspec(dllexport)
#else
#define ORACLE_EXPORT __attribute__((visibility("default")))
#endif
/* Opt-in only in this isolated oracle executable; never packaged. */
ORACLE_EXPORT int64_t tz_cuda_backend_kind(void){ return 1; }
ORACLE_EXPORT int64_t tz_cuda_malloc(int64_t n){ return tz_cpu_malloc(n); }
ORACLE_EXPORT int64_t tz_cuda_free(int64_t p){ return tz_cpu_free(p); }
ORACLE_EXPORT int64_t tz_cuda_memcpy(int64_t d,int64_t s,int64_t n,int64_t kind){ (void)kind; return tz_cpu_memcpy(d,s,n); }
ORACLE_EXPORT int64_t tz_cuda_sync(void){ return 0; }
ORACLE_EXPORT int64_t tz_cuda_device_count(void){ return 1; }
ORACLE_EXPORT int64_t tz_gpu_fill_f32(int64_t p,int64_t n,double v){ return tz_cpu_fill_f32(p,n,v); }
ORACLE_EXPORT int64_t tz_gpu_copy_f32(int64_t s,int64_t d,int64_t n){ return tz_cpu_copy_f32(s,d,n); }
ORACLE_EXPORT int64_t tz_gpu_axpy_f32(int64_t y,int64_t x,double a,int64_t n){ return tz_cpu_axpy_f32(y,x,a,n); }
ORACLE_EXPORT int64_t tz_gpu_add_f32(int64_t a,int64_t b,int64_t o,int64_t n){ return tz_cpu_add_f32(a,b,o,n); }
ORACLE_EXPORT int64_t tz_gpu_sub_f32(int64_t a,int64_t b,int64_t o,int64_t n){ return tz_cpu_sub_f32(a,b,o,n); }
ORACLE_EXPORT int64_t tz_gpu_mul_f32(int64_t a,int64_t b,int64_t o,int64_t n){ return tz_cpu_mul_f32(a,b,o,n); }
ORACLE_EXPORT int64_t tz_gpu_scale_f32(int64_t a,int64_t o,int64_t n,double s){ return tz_cpu_scale_f32(a,o,n,s); }
ORACLE_EXPORT int64_t tz_gpu_relu_f32(int64_t a,int64_t o,int64_t n){ return tz_cpu_relu_f32(a,o,n); }
ORACLE_EXPORT int64_t tz_gpu_relu_bwd_f32(int64_t g,int64_t x,int64_t o,int64_t n){ return tz_cpu_relu_bwd_f32(g,x,o,n); }
ORACLE_EXPORT int64_t tz_gpu_gelu_f32(int64_t a,int64_t o,int64_t n){ return tz_cpu_gelu_f32(a,o,n); }
ORACLE_EXPORT int64_t tz_gpu_gelu_bwd_f32(int64_t g,int64_t x,int64_t o,int64_t n){ return tz_cpu_gelu_bwd_f32(g,x,o,n); }
ORACLE_EXPORT int64_t tz_gpu_softmax_f32(int64_t x,int64_t r,int64_t cols){ return tz_cpu_softmax_f32(x,r,cols); }
ORACLE_EXPORT int64_t tz_gpu_add_bias_f32(int64_t x,int64_t b,int64_t r,int64_t cols){ return tz_cpu_add_bias_f32(x,b,r,cols); }
ORACLE_EXPORT int64_t tz_gpu_gemm_f32(int64_t a,int64_t b,int64_t o,int64_t m,int64_t k,int64_t n,double alpha,double beta){ return tz_cpu_gemm_f32(a,b,o,m,k,n,alpha,beta); }
ORACLE_EXPORT int64_t tz_gpu_gemm_nt_f32(int64_t a,int64_t b,int64_t o,int64_t m,int64_t k,int64_t n,double alpha,double beta){ return tz_cpu_gemm_nt_f32(a,b,o,m,k,n,alpha,beta); }
ORACLE_EXPORT int64_t tz_gpu_gemm_tn_f32(int64_t a,int64_t b,int64_t o,int64_t m,int64_t k,int64_t n,double alpha,double beta){ return tz_cpu_gemm_tn_f32(a,b,o,m,k,n,alpha,beta); }
ORACLE_EXPORT int64_t tz_gpu_reduce_sum_cols_f32(int64_t x,int64_t o,int64_t r,int64_t cols){ return tz_cpu_reduce_sum_cols_f32(x,o,r,cols); }
ORACLE_EXPORT int64_t tz_gpu_cross_entropy_f32(int64_t x,int64_t labels,int64_t loss,int64_t b,int64_t classes){
  int64_t copy=tz_cpu_zeros(b*classes); if(!copy) return -1;
  tz_cpu_copy_f32(x,copy,b*classes);
  int64_t status=tz_cpu_cross_entropy_f32(copy,labels,loss,b,classes);
  tz_cpu_free(copy); return status;
}
ORACLE_EXPORT int64_t tz_gpu_cross_entropy_bwd_f32(int64_t x,int64_t labels,int64_t grad,int64_t b,int64_t classes){
  int64_t copy=tz_cpu_zeros(b*classes); if(!copy) return -1;
  tz_cpu_copy_f32(x,copy,b*classes); tz_cpu_softmax_f32(copy,b,classes);
  int64_t status=tz_cpu_cross_entropy_bwd_f32(copy,labels,grad,b,classes);
  tz_cpu_free(copy); return status;
}
ORACLE_EXPORT int64_t tz_gpu_adam_f32(int64_t p,int64_t g,int64_t m,int64_t v,double lr,double b1,double b2,double eps,double b1t,double b2t,int64_t n){ return tz_cpu_adam_f32(p,g,m,v,lr,b1,b2,eps,b1t,b2t,n); }
ORACLE_EXPORT int64_t tz_gpu_sgd_f32(int64_t p,int64_t g,int64_t vel,double lr,double momentum,double wd,int64_t n){ return tz_cpu_sgd_f32(p,g,vel,lr,momentum,wd,n); }
ORACLE_EXPORT int64_t tz_gpu_xavier_init_f32(int64_t w,int64_t n,int64_t fan_in,int64_t seed){ return tz_cpu_xavier_init_f32(w,n,fan_in,seed); }
ORACLE_EXPORT int64_t tz_gpu_causal_mask_f32(int64_t scores,int64_t heads,int64_t seq){
  float* p=fp(scores);
  for(int64_t h=0;h<heads;h++) for(int64_t r=0;r<seq;r++) for(int64_t col=r+1;col<seq;col++) p[h*seq*seq+r*seq+col]=-1e9f;
  return 0;
}
