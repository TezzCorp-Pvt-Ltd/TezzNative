/* Host-only arithmetic oracle shim. This does not emulate a CUDA device. */
#define __host__
#define __device__
#define __global__
#define __forceinline__ inline
