#pragma once
#include "../temporal_shape.h"

__global__ void k_temporal_finite(const float* x,int64_t count,int* invalid){
  int64_t i=(int64_t)blockIdx.x*blockDim.x+threadIdx.x;
  if(i<count && !isfinite(x[i])) atomicExch(invalid,1);
}

// Checked temporal operations synchronize to report nonfinite arithmetic and
// device failures. No model state, optimizer, host numeric fallback or im2col
// workspace is hidden here. Each gradient element has one owning thread.
static bool tz_temporal_finite_gpu(const int64_t* pointers,const int64_t* counts,int n){
  int* invalid=NULL;
  if(cudaMalloc(&invalid,sizeof(int))!=cudaSuccess) return false;
  cudaError_t status=cudaMemset(invalid,0,sizeof(int));
  for(int i=0;i<n && status==cudaSuccess;i++) if(pointers[i]){
    k_temporal_finite<<<(counts[i]+BLOCK_1D-1)/BLOCK_1D,BLOCK_1D>>>((float*)pointers[i],counts[i],invalid);
    status=cudaGetLastError();
  }
  int result=1;
  if(status==cudaSuccess) status=cudaMemcpy(&result,invalid,sizeof(int),cudaMemcpyDeviceToHost);
  cudaError_t released=cudaFree(invalid);
  return status==cudaSuccess && released==cudaSuccess && result==0;
}

__global__ void k_conv1d_f32(const float* x,const float* w,const float* bias,float* out,TzTemporalShape q){
  int64_t index=(int64_t)blockIdx.x*blockDim.x+threadIdx.x;
  if(index>=q.ny) return;
  int o=(int)(index%q.O),u=(int)(index/q.O%q.U),b=(int)(index/q.O/q.U);
  float sum=bias ? bias[o] : 0.f;
  for(int k=0;k<q.K;k++){
    int64_t t=(int64_t)u*q.S-q.left+(int64_t)k*q.D;
    if(t<0 || t>=q.T) continue;
    for(int i=0;i<q.I;i++) sum+=x[((int64_t)b*q.T+t)*q.I+i]*w[((int64_t)o*q.K+k)*q.I+i];
  }
  out[index]=sum;
}

__global__ void k_conv1d_dx(const float* dy,const float* w,float* dx,TzTemporalShape q){
  int64_t index=(int64_t)blockIdx.x*blockDim.x+threadIdx.x;
  if(index>=q.nx) return;
  int i=(int)(index%q.I),t=(int)(index/q.I%q.T),b=(int)(index/q.I/q.T);
  float sum=dx[index];
  for(int k=0;k<q.K;k++){
    int64_t numerator=t+q.left-(int64_t)k*q.D;
    if(numerator<0 || numerator%q.S) continue;
    int64_t u=numerator/q.S;
    if(u>=q.U) continue;
    for(int o=0;o<q.O;o++) sum+=dy[((int64_t)b*q.U+u)*q.O+o]*w[((int64_t)o*q.K+k)*q.I+i];
  }
  dx[index]=sum;
}

__global__ void k_conv1d_dw(const float* dy,const float* x,float* dw,TzTemporalShape q){
  int64_t index=(int64_t)blockIdx.x*blockDim.x+threadIdx.x;
  if(index>=q.nw) return;
  int i=(int)(index%q.I),k=(int)(index/q.I%q.K),o=(int)(index/q.I/q.K);
  float sum=dw[index];
  for(int b=0;b<q.B;b++) for(int u=0;u<q.U;u++){
    int64_t t=(int64_t)u*q.S-q.left+(int64_t)k*q.D;
    if(t>=0 && t<q.T) sum+=dy[((int64_t)b*q.U+u)*q.O+o]*x[((int64_t)b*q.T+t)*q.I+i];
  }
  dw[index]=sum;
}

__global__ void k_conv1d_db(const float* dy,float* db,TzTemporalShape q){
  int64_t o=(int64_t)blockIdx.x*blockDim.x+threadIdx.x;
  if(o>=q.O) return;
  float sum=db[o];
  for(int b=0;b<q.B;b++) for(int u=0;u<q.U;u++) sum+=dy[((int64_t)b*q.U+u)*q.O+o];
  db[o]=sum;
}
