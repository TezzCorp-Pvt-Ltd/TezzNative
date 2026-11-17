/* Raw NPU buffers and generic shaders are not implemented by the model provider. */
TN_EXPORT unsigned char* tn_npu_alloc(long long bytes){ (void)bytes; return NULL; }
TN_EXPORT long long tn_npu_free(unsigned char* p){ (void)p; return 0; }
TN_EXPORT long long tn_npu_memcpy_to_device(unsigned char* dst, unsigned char* src, long long bytes){
  (void)dst; (void)src; (void)bytes;
  return -1;
}
TN_EXPORT long long tn_npu_memcpy_to_host(unsigned char* dst, unsigned char* src, long long bytes){
  (void)dst; (void)src; (void)bytes;
  return -1;
}
TN_EXPORT long long tn_npu_memset(unsigned char* dst, long long value, long long bytes){
  (void)dst; (void)value; (void)bytes;
  return -1;
}
TN_EXPORT long long tn_npu_launch_1d(long long kernel_id, unsigned char* buffer, long long count){
  (void)kernel_id; (void)buffer; (void)count;
  return -1;
}
TN_EXPORT long long tn_npu_launch_dxil_1d(unsigned char* bytecode, long long length, unsigned char* buffer, long long count){
  (void)bytecode; (void)length; (void)buffer; (void)count;
  return -1;
}
TN_EXPORT long long tn_npu_supports_dxil(void){ return 0; }
