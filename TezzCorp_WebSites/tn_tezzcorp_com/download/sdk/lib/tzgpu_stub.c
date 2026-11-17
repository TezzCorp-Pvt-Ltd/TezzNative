// Unavailable CUDA backend. Probing reports no device; computation fails.
// Signatures match tzgpu_kernels.cu, including scalar widths and argument order.
#include <stdint.h>
#if defined(TN_NATIVE_X64_ABI)
#  include "../include/native_abi.h"
#  define TZ_EXPORT TN_NATIVE_API
#elif defined(_WIN32)
#  define TZ_EXPORT __declspec(dllexport)
#else
#  define TZ_EXPORT __attribute__((visibility("default")))
#endif

TZ_EXPORT int64_t tz_cuda_backend_kind(void){ return 0; }
TZ_EXPORT int64_t tz_cuda_malloc(int64_t nbytes){ (void)nbytes; return 0; }
TZ_EXPORT int64_t tz_cuda_free(int64_t ptr){ (void)ptr; return ptr ? -1 : 0; }
TZ_EXPORT int64_t tz_cuda_memcpy(int64_t dst, int64_t src, int64_t n, int64_t kind){ (void)dst; (void)src; (void)n; (void)kind; return -1; }
TZ_EXPORT int64_t tz_cuda_sync(void){  return -1; }
TZ_EXPORT int64_t tz_cuda_device_count(void){  return 0; }
TZ_EXPORT int64_t tz_cuda_set_device(int64_t idx){ (void)idx; return -1; }
TZ_EXPORT int64_t tz_cuda_mem_free(void){  return 0; }
TZ_EXPORT int64_t tz_cuda_mem_total(void){  return 0; }
TZ_EXPORT int64_t tz_gpu_add_f32(int64_t a, int64_t b, int64_t c, int64_t n){ (void)a; (void)b; (void)c; (void)n; return -1; }
TZ_EXPORT int64_t tz_gpu_sub_f32(int64_t a, int64_t b, int64_t c, int64_t n){ (void)a; (void)b; (void)c; (void)n; return -1; }
TZ_EXPORT int64_t tz_gpu_mul_f32(int64_t a, int64_t b, int64_t c, int64_t n){ (void)a; (void)b; (void)c; (void)n; return -1; }
TZ_EXPORT int64_t tz_gpu_scale_f32(int64_t a, int64_t c, int64_t n, double s){ (void)a; (void)c; (void)n; (void)s; return -1; }
TZ_EXPORT int64_t tz_gpu_fill_f32(int64_t a, int64_t n, double val){ (void)a; (void)n; (void)val; return -1; }
TZ_EXPORT int64_t tz_gpu_copy_f32(int64_t src, int64_t dst, int64_t n){ (void)src; (void)dst; (void)n; return -1; }
TZ_EXPORT int64_t tz_gpu_axpy_f32(int64_t y, int64_t x, double alpha, int64_t n){ (void)y; (void)x; (void)alpha; (void)n; return -1; }
TZ_EXPORT int64_t tz_gpu_clamp_f32(int64_t a, int64_t n, double lo, double hi){ (void)a; (void)n; (void)lo; (void)hi; return -1; }
TZ_EXPORT int64_t tz_gpu_relu_f32(int64_t a, int64_t c, int64_t n){ (void)a; (void)c; (void)n; return -1; }
TZ_EXPORT int64_t tz_gpu_relu_bwd_f32(int64_t grad_out, int64_t x, int64_t grad_in, int64_t n){ (void)grad_out; (void)x; (void)grad_in; (void)n; return -1; }
TZ_EXPORT int64_t tz_gpu_leaky_relu_f32(int64_t a, int64_t c, int64_t n, double alpha){ (void)a; (void)c; (void)n; (void)alpha; return -1; }
TZ_EXPORT int64_t tz_gpu_sigmoid_f32(int64_t a, int64_t c, int64_t n){ (void)a; (void)c; (void)n; return -1; }
TZ_EXPORT int64_t tz_gpu_sigmoid_bwd_f32(int64_t grad_out, int64_t sig_out, int64_t grad_in, int64_t n){ (void)grad_out; (void)sig_out; (void)grad_in; (void)n; return -1; }
TZ_EXPORT int64_t tz_gpu_tanh_f32(int64_t a, int64_t c, int64_t n){ (void)a; (void)c; (void)n; return -1; }
TZ_EXPORT int64_t tz_gpu_tanh_bwd_f32(int64_t grad_out, int64_t tanh_out, int64_t grad_in, int64_t n){ (void)grad_out; (void)tanh_out; (void)grad_in; (void)n; return -1; }
TZ_EXPORT int64_t tz_gpu_gelu_f32(int64_t a, int64_t c, int64_t n){ (void)a; (void)c; (void)n; return -1; }
TZ_EXPORT int64_t tz_gpu_gelu_bwd_f32(int64_t grad_out, int64_t x, int64_t grad_in, int64_t n){ (void)grad_out; (void)x; (void)grad_in; (void)n; return -1; }
TZ_EXPORT int64_t tz_gpu_softmax_f32(int64_t x, int64_t rows, int64_t cols){ (void)x; (void)rows; (void)cols; return -1; }
TZ_EXPORT int64_t tz_gpu_add_bias_f32(int64_t x, int64_t b, int64_t batch, int64_t dim){ (void)x; (void)b; (void)batch; (void)dim; return -1; }
TZ_EXPORT int64_t tz_gpu_layernorm_f32(int64_t x, int64_t gamma, int64_t beta, int64_t rows, int64_t cols, double eps){ (void)x; (void)gamma; (void)beta; (void)rows; (void)cols; (void)eps; return -1; }
TZ_EXPORT int64_t tz_gpu_layernorm_bwd_f32(int64_t dx, int64_t dy, int64_t x, int64_t gamma, int64_t rows, int64_t cols, double eps){ (void)dx; (void)dy; (void)x; (void)gamma; (void)rows; (void)cols; (void)eps; return -1; }
TZ_EXPORT int64_t tz_gpu_batchnorm_f32(int64_t x, int64_t mean, int64_t var, int64_t gamma, int64_t beta, int64_t N, int64_t C, int64_t HW, double eps){ (void)x; (void)mean; (void)var; (void)gamma; (void)beta; (void)N; (void)C; (void)HW; (void)eps; return -1; }
TZ_EXPORT int64_t tz_gpu_dropout_f32(int64_t x, int64_t mask, int64_t n, double rate, int64_t seed){ (void)x; (void)mask; (void)n; (void)rate; (void)seed; return -1; }
TZ_EXPORT int64_t tz_gpu_embedding_f32(int64_t indices, int64_t table, int64_t out, int64_t n, int64_t embed_dim){ (void)indices; (void)table; (void)out; (void)n; (void)embed_dim; return -1; }
TZ_EXPORT int64_t tz_gpu_embedding_bwd_f32(int64_t indices, int64_t grad_out, int64_t grad_table, int64_t n, int64_t embed_dim){ (void)indices; (void)grad_out; (void)grad_table; (void)n; (void)embed_dim; return -1; }
TZ_EXPORT int64_t tz_gpu_rope_f32(int64_t x, int64_t seq, int64_t heads, int64_t dim_head){ (void)x; (void)seq; (void)heads; (void)dim_head; return -1; }
TZ_EXPORT int64_t tz_gpu_causal_mask_f32(int64_t scores, int64_t batch_heads, int64_t seq){ (void)scores; (void)batch_heads; (void)seq; return -1; }
TZ_EXPORT int64_t tz_gpu_cross_entropy_f32(int64_t logits, int64_t labels, int64_t loss_buf, int64_t batch, int64_t classes){ (void)logits; (void)labels; (void)loss_buf; (void)batch; (void)classes; return -1; }
TZ_EXPORT int64_t tz_gpu_cross_entropy_bwd_f32(int64_t logits, int64_t labels, int64_t grad, int64_t batch, int64_t classes){ (void)logits; (void)labels; (void)grad; (void)batch; (void)classes; return -1; }
TZ_EXPORT int64_t tz_gpu_mse_loss_f32(int64_t pred, int64_t target, int64_t loss_buf, int64_t n){ (void)pred; (void)target; (void)loss_buf; (void)n; return -1; }
TZ_EXPORT int64_t tz_gpu_mse_bwd_f32(int64_t pred, int64_t target, int64_t grad, int64_t n){ (void)pred; (void)target; (void)grad; (void)n; return -1; }
TZ_EXPORT int64_t tz_gpu_adam_f32(int64_t p, int64_t g, int64_t m, int64_t v, double lr, double b1, double b2, double eps, double b1t, double b2t, int64_t n){ (void)p; (void)g; (void)m; (void)v; (void)lr; (void)b1; (void)b2; (void)eps; (void)b1t; (void)b2t; (void)n; return -1; }
TZ_EXPORT int64_t tz_gpu_sgd_f32(int64_t p, int64_t g, int64_t vel, double lr, double momentum, double wd, int64_t n){ (void)p; (void)g; (void)vel; (void)lr; (void)momentum; (void)wd; (void)n; return -1; }
TZ_EXPORT int64_t tz_gpu_reduce_sum_sq_f32(int64_t x, int64_t out_buf, int64_t n){ (void)x; (void)out_buf; (void)n; return -1; }
TZ_EXPORT int64_t tz_gpu_grad_clip_f32(int64_t g, int64_t n, double max_norm, double actual_norm){ (void)g; (void)n; (void)max_norm; (void)actual_norm; return -1; }
TZ_EXPORT int64_t tz_gpu_f32_to_f64(int64_t src, int64_t dst, int64_t n){ (void)src; (void)dst; (void)n; return -1; }
TZ_EXPORT int64_t tz_gpu_f64_to_f32(int64_t src, int64_t dst, int64_t n){ (void)src; (void)dst; (void)n; return -1; }
TZ_EXPORT int64_t tz_gpu_gemm_f32(int64_t a, int64_t b, int64_t c, int64_t M, int64_t K, int64_t N, double alpha, double beta){ (void)a; (void)b; (void)c; (void)M; (void)K; (void)N; (void)alpha; (void)beta; return -1; }
TZ_EXPORT int64_t tz_gpu_add(int64_t a, int64_t b, int64_t c, int64_t n){ (void)a; (void)b; (void)c; (void)n; return -1; }
TZ_EXPORT int64_t tz_gpu_mul(int64_t a, int64_t b, int64_t c, int64_t n){ (void)a; (void)b; (void)c; (void)n; return -1; }
TZ_EXPORT int64_t tz_gpu_relu(int64_t a, int64_t c, int64_t n){ (void)a; (void)c; (void)n; return -1; }
TZ_EXPORT int64_t tz_gpu_scale(int64_t a, int64_t c, int64_t n, double s){ (void)a; (void)c; (void)n; (void)s; return -1; }
TZ_EXPORT int64_t tz_gpu_gelu(int64_t a, int64_t c, int64_t n){ (void)a; (void)c; (void)n; return -1; }
TZ_EXPORT int64_t tz_gpu_sigmoid(int64_t a, int64_t c, int64_t n){ (void)a; (void)c; (void)n; return -1; }
TZ_EXPORT int64_t tz_gpu_gemm_f64(int64_t a, int64_t b, int64_t c, int64_t M, int64_t K, int64_t N, double alpha, double beta){ (void)a; (void)b; (void)c; (void)M; (void)K; (void)N; (void)alpha; (void)beta; return -1; }
TZ_EXPORT int64_t tz_gpu_matmul_tiled(int64_t a, int64_t b, int64_t c, int64_t M, int64_t K, int64_t N){ (void)a; (void)b; (void)c; (void)M; (void)K; (void)N; return -1; }
TZ_EXPORT int64_t tz_gpu_softmax(int64_t x, int64_t rows, int64_t cols){ (void)x; (void)rows; (void)cols; return -1; }
TZ_EXPORT int64_t tz_gpu_layer_norm(int64_t x, int64_t rows, int64_t cols, double eps){ (void)x; (void)rows; (void)cols; (void)eps; return -1; }
TZ_EXPORT int64_t tz_gpu_info(void){  return -1; }
TZ_EXPORT int64_t tz_gpu_gemm_nt_f32(int64_t A, int64_t B, int64_t C, int64_t M, int64_t K, int64_t N, double alpha, double beta){ (void)A; (void)B; (void)C; (void)M; (void)K; (void)N; (void)alpha; (void)beta; return -1; }
TZ_EXPORT int64_t tz_gpu_gemm_tn_f32(int64_t A, int64_t B, int64_t C, int64_t M, int64_t K, int64_t N, double alpha, double beta){ (void)A; (void)B; (void)C; (void)M; (void)K; (void)N; (void)alpha; (void)beta; return -1; }
TZ_EXPORT int64_t tz_gpu_reduce_sum_cols_f32(int64_t X, int64_t out, int64_t rows, int64_t cols){ (void)X; (void)out; (void)rows; (void)cols; return -1; }
TZ_EXPORT int64_t tz_gpu_xavier_init_f32(int64_t W, int64_t n, int64_t fan_in, int64_t seed){ (void)W; (void)n; (void)fan_in; (void)seed; return -1; }
TZ_EXPORT int64_t tz_gpu_conv1d_f32(int64_t x, int64_t w, int64_t bias, int64_t out,
  int64_t B, int64_t T, int64_t Cin, int64_t Cout, int64_t K, int64_t stride, int64_t dilation){
  (void)x; (void)w; (void)bias; (void)out; (void)B; (void)T; (void)Cin; (void)Cout;
  (void)K; (void)stride; (void)dilation; return -1;
}
TZ_EXPORT int64_t tz_gpu_conv1d_bwd_f32(int64_t dy, int64_t x, int64_t w,
  int64_t dx, int64_t dw, int64_t db, int64_t B, int64_t T, int64_t Cin, int64_t Cout,
  int64_t K, int64_t stride, int64_t dilation){
  (void)dy; (void)x; (void)w; (void)dx; (void)dw; (void)db; (void)B; (void)T;
  (void)Cin; (void)Cout; (void)K; (void)stride; (void)dilation; return -1;
}
