#pragma once
#include "../temporal_shape.h"

static int tz_temporal_finite_cpu(int64_t pointer,int64_t count){
  if(!pointer) return 1;
  const float* values=fp(pointer);
  for(int64_t i=0;i<count;i++) if(!isfinite(values[i])) return 0;
  return 1;
}

TZ_CPU_EXPORT int64_t tz_cpu_conv1d_f32(int64_t x,int64_t w,int64_t bias,int64_t out,
  int64_t B,int64_t T,int64_t Cin,int64_t Cout,int64_t K,int64_t stride,int64_t dilation){
  TzTemporalShape q;
  if(!tz_temporal_shape(&q,B,T,Cin,Cout,K,stride,dilation) ||
     !tz_temporal_pointer(x,q.nx) || !tz_temporal_pointer(w,q.nw) ||
     !tz_temporal_pointer(out,q.ny) || (bias && !tz_temporal_pointer(bias,q.O))) return -1;
  const int64_t inputs[3]={x,w,bias},sizes[3]={q.nx,q.nw,q.O};
  if(!tz_temporal_outputs(inputs,sizes,3,&out,&q.ny,1)) return -1;
  for(int i=0;i<3;i++) if(!tz_temporal_finite_cpu(inputs[i],sizes[i])) return -1;
  const float *xp=fp(x),*wp=fp(w),*bp=bias ? fp(bias) : NULL;
  float* yp=fp(out);
  for(int b=0;b<q.B;b++) for(int u=0;u<q.U;u++) for(int o=0;o<q.O;o++){
    float sum=bp ? bp[o] : 0.f;
    for(int k=0;k<q.K;k++){
      int64_t t=(int64_t)u*q.S-q.left+(int64_t)k*q.D;
      if(t<0 || t>=q.T) continue;
      for(int i=0;i<q.I;i++) sum+=xp[((int64_t)b*q.T+t)*q.I+i]*wp[((int64_t)o*q.K+k)*q.I+i];
    }
    yp[((int64_t)b*q.U+u)*q.O+o]=sum;
  }
  return tz_temporal_finite_cpu(out,q.ny) ? 0 : -1;
}

/* All requested gradients accumulate; callers zero buffers for a fresh pass.
 * dx/dw/db may individually be null, but at least one must be requested. */
TZ_CPU_EXPORT int64_t tz_cpu_conv1d_bwd_f32(int64_t dy,int64_t x,int64_t w,
  int64_t dx,int64_t dw,int64_t db,int64_t B,int64_t T,int64_t Cin,int64_t Cout,
  int64_t K,int64_t stride,int64_t dilation){
  TzTemporalShape q;
  if(!tz_temporal_shape(&q,B,T,Cin,Cout,K,stride,dilation) || (!dx && !dw && !db) ||
     !tz_temporal_pointer(x,q.nx) || !tz_temporal_pointer(w,q.nw) || !tz_temporal_pointer(dy,q.ny)) return -1;
  const int64_t inputs[3]={x,w,dy},ni[3]={q.nx,q.nw,q.ny};
  const int64_t outputs[3]={dx,dw,db},no[3]={q.nx,q.nw,q.O};
  if(!tz_temporal_outputs(inputs,ni,3,outputs,no,3)) return -1;
  for(int i=0;i<3;i++) if(!tz_temporal_finite_cpu(inputs[i],ni[i]) || !tz_temporal_finite_cpu(outputs[i],no[i])) return -1;
  const float *xp=fp(x),*wp=fp(w),*gp=fp(dy);
  float *dxp=dx ? fp(dx) : NULL,*dwp=dw ? fp(dw) : NULL,*dbp=db ? fp(db) : NULL;
  for(int b=0;b<q.B;b++) for(int u=0;u<q.U;u++) for(int o=0;o<q.O;o++){
    float g=gp[((int64_t)b*q.U+u)*q.O+o];
    if(dbp) dbp[o]+=g;
    for(int k=0;k<q.K;k++){
      int64_t t=(int64_t)u*q.S-q.left+(int64_t)k*q.D;
      if(t<0 || t>=q.T) continue;
      for(int i=0;i<q.I;i++){
        int64_t xi=((int64_t)b*q.T+t)*q.I+i,wi=((int64_t)o*q.K+k)*q.I+i;
        if(dxp) dxp[xi]+=g*wp[wi];
        if(dwp) dwp[wi]+=g*xp[xi];
      }
    }
  }
  for(int i=0;i<3;i++) if(!tz_temporal_finite_cpu(outputs[i],no[i])) return -1;
  return 0;
}
