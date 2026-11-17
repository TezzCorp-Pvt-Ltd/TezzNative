// Deliberately fail one isolated CUDA context to verify error propagation.
#include <cuda_runtime.h>
__global__ void fail_context(){ asm("trap;"); }
extern "C" int trigger_failure(){
  fail_context<<<1,1>>>();
  return (int)cudaGetLastError();
}
