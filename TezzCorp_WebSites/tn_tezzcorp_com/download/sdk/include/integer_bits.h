#pragma once
#include <stdint.h>
#include <limits.h>

/* The IR's scalar integer lane is a 64-bit two's-complement bit pattern.
 * Use unsigned arithmetic so host C optimization cannot exploit signed UB. */
static inline long long tn_i64_bits(uint64_t bits){
  return bits<=INT64_MAX ? (long long)bits : -1LL-(long long)(UINT64_MAX-bits);
}
static inline long long tn_i64_add(long long a, long long b){
  return tn_i64_bits((uint64_t)a+(uint64_t)b);
}
static inline long long tn_i64_sub(long long a, long long b){
  return tn_i64_bits((uint64_t)a-(uint64_t)b);
}
static inline long long tn_i64_mul(long long a, long long b){
  return tn_i64_bits((uint64_t)a*(uint64_t)b);
}
static inline long long tn_i64_shl(long long a, long long b){
  return tn_i64_bits((uint64_t)a<<((uint64_t)b&63));
}
static inline long long tn_i64_shr(long long a, long long b){
  unsigned count=(unsigned)((uint64_t)b&63);
  uint64_t result=(uint64_t)a>>count;
  if(a<0 && count) result|=UINT64_MAX<<(64-count);
  return tn_i64_bits(result);
}
