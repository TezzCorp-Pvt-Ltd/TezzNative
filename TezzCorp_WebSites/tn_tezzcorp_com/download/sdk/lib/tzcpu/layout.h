#pragma once

// ── 19. Diagnostic & Validation Tools ─────────────────────────────────────────
// Print tensor summary and first N elements
TZ_CPU_EXPORT int64_t tz_cpu_print_tensor_f32(int64_t ptr, int64_t n, int64_t max_print){
  float *p = fp(ptr);
  if(!p || n <= 0){ printf("[tensor] null or empty (n=%lld)\n", (long long)n); return 0; }
  float mn = p[0], mx = p[0];
  double sum = 0.0;
  for(int64_t i = 0; i < n; i++){
    if(p[i] < mn) mn = p[i];
    if(p[i] > mx) mx = p[i];
    sum += (double)p[i];
  }
  printf("[tensor size=%lld min=%.5f max=%.5f mean=%.5f]\n  [", (long long)n, mn, mx, (float)(sum / (double)n));
  int64_t k = (max_print > 0 && max_print < n) ? max_print : (n > 8 ? 8 : n);
  for(int64_t i = 0; i < k; i++) printf("%.4f%s", p[i], (i < k - 1) ? ", " : "");
  if(k < n) printf(", ... +%lld more", (long long)(n - k));
  printf("]\n");
  return 0;
}

static void print_indent(int spaces){
  for(int i = 0; i < spaces; i++) putchar(' ');
}

// PyTorch-style rich multi-dimensional tensor printing
TZ_CPU_EXPORT int64_t tz_cpu_print_tensor_formatted(
    int64_t ptr, int64_t d0, int64_t d1, int64_t d2, int64_t d3,
    int64_t ndim, int64_t device, int64_t requires_grad)
{
  float *p = fp(ptr);
  if(!p){ printf("tensor(null)\n"); return 0; }

  if(ndim <= 1){
    printf("tensor([");
    int64_t n = d0;
    if(n <= 8){
      for(int64_t i = 0; i < n; i++) printf("%.4f%s", p[i], (i < n - 1) ? ", " : "");
    } else {
      for(int64_t i = 0; i < 3; i++) printf("%.4f, ", p[i]);
      printf("..., ");
      for(int64_t i = n - 3; i < n; i++) printf("%.4f%s", p[i], (i < n - 1) ? ", " : "");
    }
    printf("]");
  } else if(ndim == 2){
    printf("tensor([");
    int64_t rows = d0;
    int64_t cols = d1;
    int truncate_rows = (rows > 8);
    int64_t r_limit = truncate_rows ? 3 : rows;

    for(int64_t r = 0; r < r_limit; r++){
      if(r > 0) print_indent(8);
      printf("[");
      if(cols <= 8){
        for(int64_t c = 0; c < cols; c++) printf("%7.4f%s", p[r * cols + c], (c < cols - 1) ? ", " : "");
      } else {
        for(int64_t c = 0; c < 3; c++) printf("%7.4f, ", p[r * cols + c]);
        printf("  ..., ");
        for(int64_t c = cols - 3; c < cols; c++) printf("%7.4f%s", p[r * cols + c], (c < cols - 1) ? ", " : "");
      }
      printf("]%s\n", (r < rows - 1) ? "," : "");
    }
    if(truncate_rows){
      print_indent(8); printf("...,\n");
      for(int64_t r = rows - 3; r < rows; r++){
        print_indent(8);
        printf("[");
        if(cols <= 8){
          for(int64_t c = 0; c < cols; c++) printf("%7.4f%s", p[r * cols + c], (c < cols - 1) ? ", " : "");
        } else {
          for(int64_t c = 0; c < 3; c++) printf("%7.4f, ", p[r * cols + c]);
          printf("  ..., ");
          for(int64_t c = cols - 3; c < cols; c++) printf("%7.4f%s", p[r * cols + c], (c < cols - 1) ? ", " : "");
        }
        printf("]%s\n", (r < rows - 1) ? "," : "");
      }
    }
    print_indent(7); printf("]");
  } else if(ndim == 3){
    printf("tensor([\n");
    int64_t planes = (d0 > 4) ? 2 : d0;
    int64_t rows = (d1 > 4) ? 4 : d1;
    int64_t cols = (d2 > 6) ? 6 : d2;
    for(int64_t pl = 0; pl < planes; pl++){
      print_indent(8); printf("[\n");
      for(int64_t r = 0; r < rows; r++){
        print_indent(10); printf("[");
        for(int64_t c = 0; c < cols; c++){
          int64_t idx = pl * (d1 * d2) + r * d2 + c;
          printf("%7.4f%s", p[idx], (c < cols - 1) ? ", " : "");
        }
        if(cols < d2) printf(", ...");
        printf("]%s\n", (r < rows - 1) ? "," : "");
      }
      if(rows < d1){ print_indent(10); printf("...\n"); }
      print_indent(8); printf("]%s\n", (pl < planes - 1) ? ",\n" : "");
    }
    if(planes < d0){ print_indent(8); printf("...\n"); }
    print_indent(7); printf("]");
  } else {
    // 4D Tensor: [d0, d1, d2, d3]
    printf("tensor([\n");
    int64_t b_max = (d0 > 2) ? 2 : d0;
    int64_t c_max = (d1 > 2) ? 2 : d1;
    int64_t h_max = (d2 > 4) ? 4 : d2;
    int64_t w_max = (d3 > 4) ? 4 : d3;
    for(int64_t b = 0; b < b_max; b++){
      print_indent(8); printf("[\n");
      for(int64_t c = 0; c < c_max; c++){
        print_indent(10); printf("[\n");
        for(int64_t h = 0; h < h_max; h++){
          print_indent(12); printf("[");
          for(int64_t w = 0; w < w_max; w++){
            int64_t idx = b * (d1 * d2 * d3) + c * (d2 * d3) + h * d3 + w;
            printf("%7.4f%s", p[idx], (w < w_max - 1) ? ", " : "");
          }
          if(w_max < d3) printf(", ...");
          printf("]%s\n", (h < h_max - 1) ? "," : "");
        }
        if(h_max < d2){ print_indent(12); printf("...\n"); }
        print_indent(10); printf("]%s\n", (c < c_max - 1) ? ",\n" : "");
      }
      if(c_max < d1){ print_indent(10); printf("...\n"); }
      print_indent(8); printf("]%s\n", (b < b_max - 1) ? ",\n" : "");
    }
    if(b_max < d0){ print_indent(8); printf("...\n"); }
    print_indent(7); printf("]");
  }

  // Footer attributes
  if(ndim == 1){
    printf(", shape=(%lld)", (long long)d0);
  } else if(ndim == 2){
    printf(", shape=(%lld, %lld)", (long long)d0, (long long)d1);
  } else if(ndim == 3){
    printf(", shape=(%lld, %lld, %lld)", (long long)d0, (long long)d1, (long long)d2);
  } else {
    printf(", shape=(%lld, %lld, %lld, %lld)", (long long)d0, (long long)d1, (long long)d2, (long long)d3);
  }
  printf(", dtype=f32");
  if(device == 1) printf(", device='cuda:0'");
  if(requires_grad == 1) printf(", requires_grad=True");
  printf(")\n");
  fflush(stdout);
  return 0;
}

// Check if all elements in tensor are finite (no NaN, no Inf)
TZ_CPU_EXPORT int64_t tz_cpu_check_finite_f32(int64_t ptr, int64_t n){
  float *p = fp(ptr);
  if(!p) return 0;
  for(int64_t i = 0; i < n; i++){
    uint32_t bits; memcpy(&bits, &p[i], 4);
    uint32_t exp = (bits >> 23) & 0xFF;
    if(exp == 0xFF) return 0; // NaN or Inf
  }
  return 1;
}

// Multi-Dimensional 4D Tensor Strided Slicing (Forward)
// Extracts sub-tensor src[s0:e0, s1:e1, s2:e2, s3:e3] into dst contiguous buffer
TZ_CPU_EXPORT int64_t tz_cpu_slice4d_f32(
    int64_t src_ptr, int64_t dst_ptr,
    int64_t s_d0, int64_t s_d1, int64_t s_d2, int64_t s_d3,
    int64_t s0, int64_t e0,
    int64_t s1, int64_t e1,
    int64_t s2, int64_t e2,
    int64_t s3, int64_t e3)
{
  float *src = fp(src_ptr);
  float *dst = fp(dst_ptr);
  if(!src || !dst) return -1;

  int64_t out_d0 = (e0 > s0) ? (e0 - s0) : 1;
  int64_t out_d1 = (e1 > s1) ? (e1 - s1) : 1;
  int64_t out_d2 = (e2 > s2) ? (e2 - s2) : 1;
  int64_t out_d3 = (e3 > s3) ? (e3 - s3) : 1;

  int64_t src_stride0 = s_d1 * s_d2 * s_d3;
  int64_t src_stride1 = s_d2 * s_d3;
  int64_t src_stride2 = s_d3;

  int64_t dst_stride0 = out_d1 * out_d2 * out_d3;
  int64_t dst_stride1 = out_d2 * out_d3;
  int64_t dst_stride2 = out_d3;

  int i0, i1, i2;
  #pragma omp parallel for private(i1, i2) schedule(static)
  for(i0 = 0; i0 < (int)out_d0; i0++){
    for(i1 = 0; i1 < (int)out_d1; i1++){
      for(i2 = 0; i2 < (int)out_d2; i2++){
        int64_t src_base = (s0 + (int64_t)i0) * src_stride0 + (s1 + (int64_t)i1) * src_stride1 + (s2 + (int64_t)i2) * src_stride2 + s3;
        int64_t dst_base = (int64_t)i0 * dst_stride0 + (int64_t)i1 * dst_stride1 + (int64_t)i2 * dst_stride2;
        memcpy(&dst[dst_base], &src[src_base], (size_t)out_d3 * sizeof(float));
      }
    }
  }
  return 0;
}

// Multi-Dimensional 4D Tensor Strided Slicing (Backward Gradient Scatter)
// Accumulates grad_out back into grad_in at [s0:e0, s1:e1, s2:e2, s3:e3]
TZ_CPU_EXPORT int64_t tz_cpu_slice4d_bwd_f32(
    int64_t grad_out_ptr, int64_t grad_in_ptr,
    int64_t s_d0, int64_t s_d1, int64_t s_d2, int64_t s_d3,
    int64_t s0, int64_t e0,
    int64_t s1, int64_t e1,
    int64_t s2, int64_t e2,
    int64_t s3, int64_t e3)
{
  float *grad_out = fp(grad_out_ptr);
  float *grad_in  = fp(grad_in_ptr);
  if(!grad_out || !grad_in) return -1;

  int64_t out_d0 = (e0 > s0) ? (e0 - s0) : 1;
  int64_t out_d1 = (e1 > s1) ? (e1 - s1) : 1;
  int64_t out_d2 = (e2 > s2) ? (e2 - s2) : 1;
  int64_t out_d3 = (e3 > s3) ? (e3 - s3) : 1;

  int64_t src_stride0 = s_d1 * s_d2 * s_d3;
  int64_t src_stride1 = s_d2 * s_d3;
  int64_t src_stride2 = s_d3;

  int64_t dst_stride0 = out_d1 * out_d2 * out_d3;
  int64_t dst_stride1 = out_d2 * out_d3;
  int64_t dst_stride2 = out_d3;

  int i0, i1, i2;
  #pragma omp parallel for private(i1, i2) schedule(static)
  for(i0 = 0; i0 < (int)out_d0; i0++){
    for(i1 = 0; i1 < (int)out_d1; i1++){
      for(i2 = 0; i2 < (int)out_d2; i2++){
        int64_t in_base  = (s0 + (int64_t)i0) * src_stride0 + (s1 + (int64_t)i1) * src_stride1 + (s2 + (int64_t)i2) * src_stride2 + s3;
        int64_t out_base = (int64_t)i0 * dst_stride0 + (int64_t)i1 * dst_stride1 + (int64_t)i2 * dst_stride2;
        for(int64_t i3 = 0; i3 < out_d3; i3++){
          grad_in[in_base + i3] += grad_out[out_base + i3];
        }
      }
    }
  }
  return 0;
}
