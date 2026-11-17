#pragma once
#include <stdint.h>
#include <stddef.h>
#include <limits.h>

/* Shared CPU/CUDA Conv1D contract: X[B,T,I], W[O,K,I], Y[B,U,O].
 * U=ceil(T/S); left padding=floor(D*(K-1)/2), right gets the remainder.
 * Counts fit signed 32-bit kernel indexing. All output buffers are disjoint
 * from inputs and from one another. Input buffers may overlap each other.
 * Raw CPU pointers must reference caller-owned buffers of the declared size.
 * Finite inputs/accumulators are required. Invalid arguments fail before
 * writes; arithmetic overflow returns -1 with output contents unspecified. */
typedef struct {
  int B,T,I,O,K,S,D,U;
  int64_t nx,nw,ny,left;
} TzTemporalShape;

static int64_t tz_temporal_count(int64_t a,int64_t b){
  int64_t limit=PTRDIFF_MAX/(int64_t)sizeof(float);
  if(limit>INT_MAX) limit=INT_MAX;
  return a>0 && b>0 && a<=limit/b ? a*b : 0;
}
static int tz_temporal_shape(TzTemporalShape* q,int64_t B,int64_t T,int64_t I,
                             int64_t O,int64_t K,int64_t S,int64_t D){
  if(B<=0 || T<=0 || I<=0 || O<=0 || K<=0 || S<=0 || D<=0 ||
     B>INT_MAX || T>INT_MAX || I>INT_MAX || O>INT_MAX || K>INT_MAX || S>INT_MAX || D>INT_MAX ||
     K-1>INT_MAX/D) return 0;
  int64_t U=(T-1)/S+1;
  q->nx=tz_temporal_count(tz_temporal_count(B,T),I);
  q->nw=tz_temporal_count(tz_temporal_count(O,K),I);
  q->ny=tz_temporal_count(tz_temporal_count(B,U),O);
  if(!q->nx || !q->nw || !q->ny) return 0;
  q->B=(int)B; q->T=(int)T; q->I=(int)I; q->O=(int)O;
  q->K=(int)K; q->S=(int)S; q->D=(int)D; q->U=(int)U;
  q->left=D*(K-1)/2;
  return 1;
}
static int tz_temporal_pointer(int64_t p,int64_t count){
  return p && count>0 && count<=INT_MAX && (uint64_t)p%sizeof(float)==0 &&
         (uint64_t)p<=UINTPTR_MAX-(uint64_t)count*sizeof(float);
}
static int tz_temporal_overlap(int64_t a,int64_t na,int64_t b,int64_t nb){
  if(!a || !b) return 0;
  uint64_t x=(uint64_t)a,y=(uint64_t)b;
  return x<=y ? y-x<(uint64_t)na*sizeof(float) : x-y<(uint64_t)nb*sizeof(float);
}
static int tz_temporal_outputs(const int64_t* input,const int64_t* ni,int inputs,
                               const int64_t* output,const int64_t* no,int outputs){
  for(int o=0;o<outputs;o++) if(output[o]){
    if(!tz_temporal_pointer(output[o],no[o])) return 0;
    for(int i=0;i<inputs;i++) if(tz_temporal_overlap(output[o],no[o],input[i],ni[i])) return 0;
    for(int p=0;p<o;p++) if(tz_temporal_overlap(output[o],no[o],output[p],no[p])) return 0;
  }
  return 1;
}
