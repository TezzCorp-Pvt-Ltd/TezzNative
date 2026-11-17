/* No dedicated provider was enabled at build time. */
TN_EXPORT long long tn_npu_has_backend(void){ return 0; }
TN_EXPORT long long tn_npu_init(void){ return -1; }
TN_EXPORT long long tn_npu_shutdown(void){ return 0; }
TN_EXPORT long long tn_npu_device_count(void){ return 0; }
TN_EXPORT long long tn_npu_set_device(long long id){ (void)id; return -1; }
TN_EXPORT unsigned char* tn_npu_backend_name(void){ return tn_dup_cstr("none"); }
TN_EXPORT long long tn_npu_model_load(unsigned char* path){
  (void)path;
  return -1;
}
TN_EXPORT long long tn_npu_model_free(long long h){
  (void)h;
  return -1;
}
TN_EXPORT long long tn_npu_model_run_f64_1d(long long h, double* input, long long input_len, double* output, long long output_len){
  (void)h; (void)input; (void)input_len; (void)output; (void)output_len;
  return -1;
}
