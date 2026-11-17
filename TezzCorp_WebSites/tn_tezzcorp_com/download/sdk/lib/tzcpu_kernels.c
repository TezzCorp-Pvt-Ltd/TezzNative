// tzcpu_kernels.c -- TezzNative CPU Kernel Library v1.0
// Portable C99 implementation — runs on any CPU: x86_64, ARM64 (Snapdragon), RISC-V
//
// Windows: cl /O2 /LD /MD tzcpu_kernels.c /Fe:tzcpu.dll /link /DEF:tzcpu.def
//          or: gcc -O3 -shared -fPIC tzcpu_kernels.c -o tzcpu.dll -lm
// Linux:   gcc -O3 -shared -fPIC -fopenmp tzcpu_kernels.c -o libtzcpu.so -lm
// ARM64 (Snapdragon WoA):
//          cl /O2 /arch:arm64 /LD /MD tzcpu_kernels.c /Fe:tzcpu.dll
//
// OpenMP is optional — if not available the code still works, just single-threaded.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <float.h>
#include "../include/float_bits.h"

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <pthread.h>
#  include <unistd.h>
#endif

#ifdef _OPENMP
#  include <omp.h>
#endif

#if defined(TN_NATIVE_X64_ABI)
#  include "../include/native_abi.h"
#  define TZ_CPU_EXPORT TN_NATIVE_API
#elif defined(_WIN32)
#  define TZ_CPU_EXPORT __declspec(dllexport)
#else
#  define TZ_CPU_EXPORT __attribute__((visibility("default")))
#endif

// ─── f32 pointer helpers ──────────────────────────────────────────────────────
static inline float* fp(int64_t p){ return (float*)(uintptr_t)p; }
static inline int*   ip(int64_t p){ return (int*  )(uintptr_t)p; }

// ─── Thread count ─────────────────────────────────────────────────────────────
static int tz_cpu_threads(){
#ifdef _OPENMP
  return omp_get_max_threads();
#else
  return 1;
#endif
}

// =============================================================================
//  EXPORTED C API — mirrors tzgpu.dll naming with tz_cpu_ prefix
// =============================================================================
#ifdef __cplusplus
extern "C" {
#endif

#include "tzcpu/core.h"
#include "tzcpu/transformer.h"
#include "tzcpu/vision.h"
#include "tzcpu/temporal.h"
#include "tzcpu/layout.h"
#include "tzcpu/gguf.h"

#include "tzcpu/platform.h"
