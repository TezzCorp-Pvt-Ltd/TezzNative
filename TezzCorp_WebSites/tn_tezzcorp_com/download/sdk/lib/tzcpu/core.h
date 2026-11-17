#pragma once

// ── Device probing ────────────────────────────────────────────────────────────
TZ_CPU_EXPORT int64_t tz_cpu_info(){
  int threads = tz_cpu_threads();
  printf("[CPU] TezzNative CPU backend v1.0\n");
  printf("[CPU] OpenMP threads: %d\n", threads);
#if defined(__aarch64__) || defined(_M_ARM64)
  printf("[CPU] Architecture: ARM64 (Snapdragon / Apple Silicon compatible)\n");
#elif defined(__x86_64__) || defined(_M_X64)
  printf("[CPU] Architecture: x86-64\n");
#else
  printf("[CPU] Architecture: unknown\n");
#endif
  return threads;
}

// Returns 1 always (CPU is always available)
TZ_CPU_EXPORT int64_t tz_cpu_device_count(){ return 1; }

// ── Memory (aligned malloc for SIMD) ─────────────────────────────────────────
TZ_CPU_EXPORT int64_t tz_cpu_malloc(int64_t nbytes){
#ifdef _WIN32
  void* p = _aligned_malloc((size_t)nbytes, 64);
#else
  void* p = NULL;
  if(posix_memalign(&p, 64, (size_t)nbytes) != 0) p = NULL;
#endif
  if(!p) return 0;
  return (int64_t)(uintptr_t)p;
}

TZ_CPU_EXPORT int64_t tz_cpu_free(int64_t ptr){
  if(ptr){
#ifdef _WIN32
    _aligned_free((void*)(uintptr_t)ptr);
#else
    free((void*)(uintptr_t)ptr);
#endif
  }
  return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_zeros(int64_t n){
  int64_t p = tz_cpu_malloc(n * 4);
  if(p) memset((void*)(uintptr_t)p, 0, (size_t)(n * 4));
  return p;
}

TZ_CPU_EXPORT int64_t tz_cpu_memcpy(int64_t dst, int64_t src, int64_t nbytes){
  memcpy((void*)(uintptr_t)dst, (void*)(uintptr_t)src, (size_t)nbytes);
  return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_sync(){ return 0; }  // no-op on CPU

// ── f32 Elementwise ───────────────────────────────────────────────────────────
TZ_CPU_EXPORT int64_t tz_cpu_fill_f32(int64_t a, int64_t n, double val){
  float v = (float)val; float* p = fp(a);
  for(int64_t i = 0; i < n; i++) p[i] = v;
  return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_copy_f32(int64_t src, int64_t dst, int64_t n){
  memcpy(fp(dst), fp(src), (size_t)(n * 4)); return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_add_f32(int64_t a, int64_t b, int64_t c, int64_t n){
  float *pa=fp(a),*pb=fp(b),*pc=fp(c);
  for(int64_t i=0;i<n;i++) pc[i]=pa[i]+pb[i]; return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_sub_f32(int64_t a, int64_t b, int64_t c, int64_t n){
  float *pa=fp(a),*pb=fp(b),*pc=fp(c);
  for(int64_t i=0;i<n;i++) pc[i]=pa[i]-pb[i]; return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_mul_f32(int64_t a, int64_t b, int64_t c, int64_t n){
  float *pa=fp(a),*pb=fp(b),*pc=fp(c);
  for(int64_t i=0;i<n;i++) pc[i]=pa[i]*pb[i]; return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_scale_f32(int64_t a, int64_t c, int64_t n, double s){
  float sv=(float)s; float *pa=fp(a),*pc=fp(c);
  for(int64_t i=0;i<n;i++) pc[i]=pa[i]*sv; return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_axpy_f32(int64_t y, int64_t x, double alpha, int64_t n){
  float a=(float)alpha; float *py=fp(y),*px=fp(x);
  for(int64_t i=0;i<n;i++) py[i]+=a*px[i]; return 0;
}

// ── Activations ───────────────────────────────────────────────────────────────
TZ_CPU_EXPORT int64_t tz_cpu_relu_f32(int64_t a, int64_t c, int64_t n){
  float *pa=fp(a),*pc=fp(c);
  for(int64_t i=0;i<n;i++) pc[i]=pa[i]>0.f?pa[i]:0.f; return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_relu_bwd_f32(int64_t grad_out, int64_t x, int64_t grad_in, int64_t n){
  float *gout=fp(grad_out),*px=fp(x),*gin=fp(grad_in);
  for(int64_t i=0;i<n;i++) gin[i]=px[i]>0.f?gout[i]:0.f; return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_sigmoid_f32(int64_t a, int64_t c, int64_t n){
  float *pa=fp(a),*pc=fp(c);
  for(int64_t i=0;i<n;i++) pc[i]=1.f/(1.f+expf(-pa[i])); return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_tanh_f32(int64_t a, int64_t c, int64_t n){
  float *pa=fp(a),*pc=fp(c);
  for(int64_t i=0;i<n;i++) pc[i]=tanhf(pa[i]); return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_gelu_f32(int64_t a, int64_t c, int64_t n){
  // GELU approx: 0.5x(1+tanh(sqrt(2/pi)(x+0.044715x^3)))
  float *pa=fp(a),*pc=fp(c);
  const float k=0.7978845608f; // sqrt(2/pi)
  for(int64_t i=0;i<n;i++){
    float x=pa[i];
    pc[i]=0.5f*x*(1.f+tanhf(k*(x+0.044715f*x*x*x)));
  }
  return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_gelu_bwd_f32(int64_t grad_out, int64_t x, int64_t grad_in, int64_t n){
  float *gout=fp(grad_out),*px=fp(x),*gin=fp(grad_in);
  const float k=0.7978845608f;
  for(int64_t i=0;i<n;i++){
    float xi=px[i];
    float t=tanhf(k*(xi+0.044715f*xi*xi*xi));
    float dt=1.f-t*t;
    float gelu_d=0.5f*(1.f+t)+0.5f*xi*dt*k*(1.f+3.f*0.044715f*xi*xi);
    gin[i]=gout[i]*gelu_d;
  }
  return 0;
}

// ── Softmax (in-place, row-wise) ──────────────────────────────────────────────
TZ_CPU_EXPORT int64_t tz_cpu_softmax_f32(int64_t x, int64_t rows, int64_t cols){
  float *px=fp(x);
  for(int64_t r=0;r<rows;r++){
    float *row=px+r*cols;
    float mx=row[0];
    for(int64_t c=1;c<cols;c++) if(row[c]>mx) mx=row[c];
    float s=0.f;
    for(int64_t c=0;c<cols;c++){ row[c]=expf(row[c]-mx); s+=row[c]; }
    for(int64_t c=0;c<cols;c++) row[c]/=s;
  }
  return 0;
}

// ── Add bias (broadcast) ──────────────────────────────────────────────────────
TZ_CPU_EXPORT int64_t tz_cpu_add_bias_f32(int64_t x, int64_t b, int64_t batch, int64_t dim){
  float *px=fp(x),*pb=fp(b);
  for(int64_t r=0;r<batch;r++)
    for(int64_t j=0;j<dim;j++) px[r*dim+j]+=pb[j];
  return 0;
}

// ── Layer normalization ───────────────────────────────────────────────────────
TZ_CPU_EXPORT int64_t tz_cpu_layernorm_f32(int64_t x, int64_t gamma, int64_t beta, int64_t rows, int64_t cols, double eps){
  float *px=fp(x),*pg=fp(gamma),*pb=fp(beta);
  float e=(float)eps;
  for(int64_t r=0;r<rows;r++){
    float *row=px+r*cols;
    float mu=0.f;
    for(int64_t c=0;c<cols;c++) mu+=row[c]; mu/=(float)cols;
    float va=0.f;
    for(int64_t c=0;c<cols;c++){ float d=row[c]-mu; va+=d*d; } va/=(float)cols;
    float inv=1.f/sqrtf(va+e);
    for(int64_t c=0;c<cols;c++)
      row[c]=(row[c]-mu)*inv*(pg?pg[c]:1.f)+(pb?pb[c]:0.f);
  }
  return 0;
}

// ── GEMM: C[M x N] = A[M x K] @ B[K x N] + beta*C  (row-major) ──────────────
// OpenMP parallelized for Snapdragon big.LITTLE cores
TZ_CPU_EXPORT int64_t tz_cpu_gemm_f32(int64_t A, int64_t B, int64_t C,
                                        int64_t M, int64_t K, int64_t N,
                                        double alpha, double beta){
  float *pA=fp(A),*pB=fp(B),*pC=fp(C);
  float fa=(float)alpha, fb=(float)beta;
  // Scale C by beta first
  if(fb==0.f) memset(pC,0,(size_t)(M*N*4));
  else if(fb!=1.f){ int64_t mn=M*N; for(int64_t i=0;i<mn;i++) pC[i]*=fb; }
  // GEMM: accumulate A @ B into C
  int m;
#ifdef _OPENMP
  #pragma omp parallel for schedule(dynamic,16)
#endif
  for(m=0;m<(int)M;m++){
    for(int64_t n=0;n<N;n++){
      float acc=0.f;
      const float *rowA=pA+m*K;
      for(int64_t k=0;k<K;k++) acc+=rowA[k]*pB[k*N+n];
      pC[m*N+n]+=fa*acc;
    }
  }
  return 0;
}

// C = A @ B^T  (for dW computation)
TZ_CPU_EXPORT int64_t tz_cpu_gemm_nt_f32(int64_t A, int64_t B, int64_t C,
                                           int64_t M, int64_t K, int64_t N,
                                           double alpha, double beta){
  float *pA=fp(A),*pB=fp(B),*pC=fp(C);
  float fa=(float)alpha, fb=(float)beta;
  if(fb==0.f) memset(pC,0,(size_t)(M*N*4));
  else if(fb!=1.f){ int64_t mn=M*N; for(int64_t i=0;i<mn;i++) pC[i]*=fb; }
  int m;
#ifdef _OPENMP
  #pragma omp parallel for schedule(dynamic,8)
#endif
  for(m=0;m<(int)M;m++){
    for(int64_t n=0;n<N;n++){
      float acc=0.f;
      const float *rowA=pA+m*K;
      const float *rowB=pB+n*K;  // B^T: col n of B^T = row n of B
      for(int64_t k=0;k<K;k++) acc+=rowA[k]*rowB[k];
      pC[m*N+n]+=fa*acc;
    }
  }
  return 0;
}

// C = A^T @ B  (for dX computation)
TZ_CPU_EXPORT int64_t tz_cpu_gemm_tn_f32(int64_t A, int64_t B, int64_t C,
                                           int64_t M, int64_t K, int64_t N,
                                           double alpha, double beta){
  float *pA=fp(A),*pB=fp(B),*pC=fp(C);
  float fa=(float)alpha, fb=(float)beta;
  if(fb==0.f) memset(pC,0,(size_t)(M*N*4));
  else if(fb!=1.f){ int64_t mn=M*N; for(int64_t i=0;i<mn;i++) pC[i]*=fb; }
  int m;
#ifdef _OPENMP
  #pragma omp parallel for schedule(dynamic,8)
#endif
  for(m=0;m<(int)M;m++){
    for(int64_t n=0;n<N;n++){
      float acc=0.f;
      for(int64_t k=0;k<K;k++) acc+=pA[k*M+m]*pB[k*N+n];  // A^T: row m of A^T = col m of A
      pC[m*N+n]+=fa*acc;
    }
  }
  return 0;
}

// ── Column-wise reduce (bias gradient) ───────────────────────────────────────
TZ_CPU_EXPORT int64_t tz_cpu_reduce_sum_cols_f32(int64_t X, int64_t out, int64_t rows, int64_t cols){
  float *px=fp(X),*po=fp(out);
  for(int64_t r=0;r<rows;r++)
    for(int64_t c=0;c<cols;c++) po[c]+=px[r*cols+c];
  return 0;
}

// ── Cross-entropy loss (with softmax, in-place) ───────────────────────────────
// logits: [batch x classes] f32 (modified in-place to become softmax probs)
// labels: [batch] int32
// loss_buf: [batch] f32 output
TZ_CPU_EXPORT int64_t tz_cpu_cross_entropy_f32(int64_t logits, int64_t labels, int64_t loss_buf, int64_t batch, int64_t classes){
  float *pl=fp(logits); int *plbl=ip(labels); float *ploss=fp(loss_buf);
  for(int64_t b=0;b<batch;b++){
    float *row=pl+b*classes;
    // Softmax
    float mx=row[0];
    for(int64_t c=1;c<classes;c++) if(row[c]>mx) mx=row[c];
    float target_logit=row[plbl[b] >= 0 && plbl[b] < classes ? plbl[b] : 0];
    float s=0.f;
    for(int64_t c=0;c<classes;c++){ row[c]=expf(row[c]-mx); s+=row[c]; }
    for(int64_t c=0;c<classes;c++) row[c]/=s;
    // Evaluate log-sum-exp before probability underflow can clip the loss.
    ploss[b]=logf(s)+(mx-target_logit);
  }
  return 0;
}

// Cross-entropy backward: grad = (softmax_probs - one_hot) / batch
// logits must have been run through cross_entropy_f32 first (now contains softmax probs)
TZ_CPU_EXPORT int64_t tz_cpu_cross_entropy_bwd_f32(int64_t logits, int64_t labels, int64_t grad, int64_t batch, int64_t classes){
  float *pl=fp(logits); int *plbl=ip(labels); float *pg=fp(grad);
  float scale=1.f/(float)batch;
  for(int64_t b=0;b<batch;b++){
    int lbl=plbl[b]; if(lbl<0||lbl>=(int)classes) lbl=0;
    for(int64_t c=0;c<classes;c++){
      float p=pl[b*classes+c];
      pg[b*classes+c]=(c==(int64_t)lbl ? p-1.f : p)*scale;
    }
  }
  return 0;
}

// ── MSE loss ──────────────────────────────────────────────────────────────────
TZ_CPU_EXPORT int64_t tz_cpu_mse_loss_f32(int64_t pred, int64_t target, int64_t loss_buf, int64_t n){
  float *pp=fp(pred),*pt=fp(target),*pl=fp(loss_buf);
  for(int64_t i=0;i<n;i++){ float d=pp[i]-pt[i]; pl[i]=d*d; }
  return 0;
}

// ── Dropout ───────────────────────────────────────────────────────────────────
TZ_CPU_EXPORT int64_t tz_cpu_dropout_f32(int64_t x, int64_t mask, int64_t n, double rate, int64_t seed){
  float *px=fp(x),*pm=fp(mask);
  float keep=(float)(1.0-rate);
  float scale=(keep>0.f)?1.f/keep:0.f;
  // Simple LCG PRNG (deterministic given seed)
  uint64_t state=(uint64_t)seed;
  for(int64_t i=0;i<n;i++){
    state=state*6364136223846793005ULL+1442695040888963407ULL;
    float u=(float)((state>>33)&0x7FFFFF)/(float)0x7FFFFF;
    pm[i]=(u<keep)?scale:0.f;
    px[i]*=pm[i];
  }
  return 0;
}

// ── Adam optimizer ────────────────────────────────────────────────────────────
// p[i] -= lr * m_hat[i] / (sqrt(v_hat[i]) + eps)
TZ_CPU_EXPORT int64_t tz_cpu_adam_f32(int64_t p, int64_t g, int64_t m, int64_t v,
                                        double lr, double b1, double b2, double eps,
                                        double b1t, double b2t, int64_t n){
  float *pp=fp(p),*pg=fp(g),*pm=fp(m),*pv=fp(v);
  float flr=(float)lr,fb1=(float)b1,fb2=(float)b2,feps=(float)eps;
  float bc1=1.f-(float)b1t, bc2=1.f-(float)b2t;
  for(int64_t i=0;i<n;i++){
    float gi=pg[i];
    pm[i]=fb1*pm[i]+(1.f-fb1)*gi;
    pv[i]=fb2*pv[i]+(1.f-fb2)*gi*gi;
    float mh=pm[i]/bc1;
    float vh=pv[i]/bc2;
    pp[i]-=flr*mh/(sqrtf(vh)+feps);
  }
  return 0;
}

// ── SGD with momentum ─────────────────────────────────────────────────────────
TZ_CPU_EXPORT int64_t tz_cpu_sgd_f32(int64_t p, int64_t g, int64_t vel, double lr, double momentum, double wd, int64_t n){
  float *pp=fp(p),*pg=fp(g),*pv=fp(vel);
  float flr=(float)lr,fm=(float)momentum,fwd=(float)wd;
  for(int64_t i=0;i<n;i++){
    float gi=pg[i]+fwd*pp[i];
    pv[i]=fm*pv[i]+gi;
    pp[i]-=flr*pv[i];
  }
  return 0;
}

// ── Xavier / He init ──────────────────────────────────────────────────────────
// Box-Muller transform for N(0, std)
TZ_CPU_EXPORT int64_t tz_cpu_xavier_init_f32(int64_t W, int64_t n, int64_t fan_in, int64_t seed){
  float *pw=fp(W);
  float std=sqrtf(2.f/(float)fan_in);
  uint64_t state=(uint64_t)(seed^0xDEADBEEF);
  const float TWO_PI=6.28318530718f;
  for(int64_t i=0;i<n;i+=2){
    state=state*6364136223846793005ULL+1442695040888963407ULL;
    float u1=(float)((state>>33)&0x7FFFFF)/(float)0x7FFFFF+1e-7f;
    state=state*6364136223846793005ULL+1442695040888963407ULL;
    float u2=(float)((state>>33)&0x7FFFFF)/(float)0x7FFFFF;
    float mag=std*sqrtf(-2.f*logf(u1));
    pw[i]=mag*cosf(TWO_PI*u2);
    if(i+1<n) pw[i+1]=mag*sinf(TWO_PI*u2);
  }
  return 0;
}

// ── Grad clipping ─────────────────────────────────────────────────────────────
TZ_CPU_EXPORT int64_t tz_cpu_grad_clip_f32(int64_t g, int64_t n, double max_norm){
  float *pg=fp(g);
  float norm=0.f;
  for(int64_t i=0;i<n;i++) norm+=pg[i]*pg[i];
  norm=sqrtf(norm);
  if(norm>(float)max_norm){
    float scale=(float)max_norm/norm;
    for(int64_t i=0;i<n;i++) pg[i]*=scale;
  }
  return 0;
}

#ifdef __cplusplus
}
#endif
