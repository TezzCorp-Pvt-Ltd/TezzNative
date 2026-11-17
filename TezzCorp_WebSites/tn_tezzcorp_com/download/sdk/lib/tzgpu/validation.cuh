#pragma once

// Query actual CUDA allocations, including valid interior pointers. Unsupported
// allocation kinds and pointers from another selected device fail explicitly.
static size_t tz_available(int64_t address){
  if(!address) return 0;
  cudaPointerAttributes attributes;
  int current=-1;
  if(cudaPointerGetAttributes(&attributes,(void*)address)!=cudaSuccess ||
     cudaGetDevice(&current)!=cudaSuccess || attributes.type!=cudaMemoryTypeDevice ||
     attributes.device!=current) return 0;
  typedef CUresult (CUDAAPI *AddressRange)(CUdeviceptr*,size_t*,CUdeviceptr);
  static AddressRange query=[](){
    void* entry=NULL;
    cudaDriverEntryPointQueryResult status;
    if(cudaGetDriverEntryPoint("cuMemGetAddressRange",&entry,cudaEnableDefault,&status)!=cudaSuccess ||
       status!=cudaDriverEntryPointSuccess) return (AddressRange)NULL;
    return (AddressRange)entry;
  }();
  if(!query) return 0;
  CUdeviceptr base=0;
  size_t bytes=0;
  CUdeviceptr pointer=(CUdeviceptr)(uint64_t)address;
  if(query(&base,&bytes,pointer)!=CUDA_SUCCESS || pointer<base || pointer-base>=bytes) return 0;
  return bytes-(size_t)(pointer-base);
}

static int64_t tz_count2(int64_t a,int64_t b){
  return a>0 && b>0 && a<=INT_MAX/b ? a*b : 0;
}
static bool tz_span(int64_t pointer,int64_t count,size_t element=4){
  return count>0 && count<=INT_MAX && (uint64_t)pointer%element==0 &&
         (uint64_t)count<=tz_available(pointer)/element;
}
static bool tz_overlap(int64_t a,size_t na,int64_t b,size_t nb){
  uint64_t x=(uint64_t)a,y=(uint64_t)b;
  return x<=y ? y-x<na : x-y<nb;
}
static bool tz_gemm_ready(int64_t a,int64_t b,int64_t c,int64_t m,int64_t k,int64_t n,size_t bytes){
  int64_t ac=tz_count2(m,k),bc=tz_count2(k,n),cc=tz_count2(m,n);
  return tz_span(a,ac,bytes) && tz_span(b,bc,bytes) && tz_span(c,cc,bytes) &&
         !tz_overlap(a,(size_t)ac*bytes,c,(size_t)cc*bytes) &&
         !tz_overlap(b,(size_t)bc*bytes,c,(size_t)cc*bytes);
}
static int64_t tz_launch_status(){
  return cudaGetLastError()==cudaSuccess ? 0 : -1;
}
static bool tz_indices(int64_t pointer,int64_t count,int64_t upper){
  if(!tz_span(pointer,count) || upper<=0 || upper>INT_MAX) return false;
  int* flag=NULL;
  if(cudaMalloc(&flag,sizeof(int))!=cudaSuccess) return false;
  int invalid=1;
  cudaError_t status=cudaMemset(flag,0,sizeof(int));
  if(status==cudaSuccess){
    k_check_indices<<<(count+BLOCK_1D-1)/BLOCK_1D,BLOCK_1D>>>((int*)pointer,(int)count,(int)upper,flag);
    status=cudaGetLastError();
    if(status==cudaSuccess) status=cudaMemcpy(&invalid,flag,sizeof(int),cudaMemcpyDeviceToHost);
  }
  cudaError_t released=cudaFree(flag);
  return status==cudaSuccess && released==cudaSuccess && invalid==0;
}
