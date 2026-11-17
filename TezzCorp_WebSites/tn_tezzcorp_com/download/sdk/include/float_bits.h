#pragma once
#include "types.h"
#include <stdint.h>
#include <string.h>

/* Match x64 truncation: NaN, infinity and out-of-range signed conversion yield
 * the integer-indefinite bit pattern instead of invoking a host C cast's UB. */
static inline long long tn_f64_to_i64(double value){
  if(!(value>=-9223372036854775808.0 && value<9223372036854775808.0)) return INT64_MIN;
  return (long long)value;
}

/* Narrow floats use storage bits in IR registers. Conversions round to nearest,
 * ties to even. FP8 conversions saturate to the largest finite value. */
static inline uint32_t tn_f32_bits(float f){
  uint32_t u;
  memcpy(&u, &f, sizeof(u));
  return u;
}

static inline float tn_f32_from_bits(uint32_t u){
  float f;
  memcpy(&f, &u, sizeof(f));
  return f;
}

static inline uint16_t tn_f32_to_bf16(float f){
  uint32_t u = tn_f32_bits(f);
  if((u & 0x7FFFFFFFu) > 0x7F800000u) return (uint16_t)((u >> 16) | 0x40u);
  u += 0x7FFF + ((u >> 16) & 1);
  return (uint16_t)(u >> 16);
}

static inline uint32_t tn_round_shift_even(uint32_t value, int shift){
  if(shift >= 32) return 0;
  if(shift <= 0) return value;
  uint32_t retained = value >> shift;
  uint32_t discarded = value & ((1u << shift) - 1u);
  uint32_t half = 1u << (shift - 1);
  return retained + (discarded > half || (discarded == half && (retained & 1u)));
}

static inline uint16_t tn_f32_to_f16(float f){
  uint32_t u = tn_f32_bits(f);
  uint32_t sign = (u >> 16) & 0x8000;
  int exponent = (int)((u >> 23) & 0xFF);
  uint32_t mant = u & 0x7FFFFF;
  if(exponent == 255) return (uint16_t)(sign | 0x7C00u | (mant ? 0x200u | (mant >> 13) : 0u));
  int exp = exponent - 127;
  if(exp < -25) return (uint16_t)sign;
  if(exp > 15) return (uint16_t)(sign | 0x7C00u);
  mant |= 0x800000u;
  if(exp < -14){
    return (uint16_t)(sign | tn_round_shift_even(mant, -1 - exp));
  }
  uint32_t rounded = ((uint32_t)(exp + 14) << 10) + tn_round_shift_even(mant, 13);
  return (uint16_t)(sign | rounded);
}

static inline float tn_f16_to_f32(uint16_t h){
  uint32_t sign = (uint32_t)(h & 0x8000) << 16;
  int exp = (h >> 10) & 0x1F;
  uint32_t mant = h & 0x3FF;
  uint32_t u;
  if(exp == 0){
    if(mant == 0) return tn_f32_from_bits(sign);
    exp = 1;
    while((mant & 0x400) == 0){ mant <<= 1; exp--; }
    mant &= 0x3FF;
    u = sign | ((uint32_t)(exp + 112) << 23) | (mant << 13);
  } else if(exp == 31){
    u = sign | 0x7F800000 | (mant << 13);
  } else {
    u = sign | ((uint32_t)(exp + 112) << 23) | (mant << 13);
  }
  return tn_f32_from_bits(u);
}

static inline uint8_t tn_f32_to_f8(float f, int e5m2){
  uint32_t u = tn_f32_bits(f);
  uint8_t sign = (uint8_t)((u >> 24) & 0x80);
  uint32_t max_bits = e5m2 ? 0x7B : 0x7E;
  int exponent = (int)((u >> 23) & 0xFF);
  uint32_t mant = u & 0x7FFFFF;
  if(exponent == 255) return (uint8_t)(sign | (mant ? 0x7F : max_bits));
  int mant_bits = e5m2 ? 2 : 3;
  int exp = exponent - 127 + (e5m2 ? 15 : 7);
  mant |= 0x800000u;
  uint32_t rounded;
  if(exp <= 0){
    int shift = 1 - exp + 23 - mant_bits;
    rounded = tn_round_shift_even(mant, shift);
  } else {
    rounded = ((uint32_t)(exp - 1) << mant_bits) + tn_round_shift_even(mant, 23 - mant_bits);
  }
  if(rounded > max_bits) rounded = max_bits;
  return (uint8_t)(sign | rounded);
}

static inline float tn_f8_to_f32(uint8_t b, int e5m2){
  float sign = (b & 0x80) ? -1.0f : 1.0f;
  int mant_bits = e5m2 ? 2 : 3;
  unsigned mant = b & ((1u << mant_bits) - 1);
  unsigned exp = (b & 0x7F) >> mant_bits;
  if(exp == 0){
    float base = e5m2 ? 0.00006103515625f : 0.015625f;
    return sign * ((float)mant / (float)(1u << mant_bits)) * base;
  }
  if((e5m2 && exp == 31) || (!e5m2 && exp == 15 && mant == 7)){
    uint32_t bits = (sign < 0 ? 0x80000000u : 0) | 0x7F800000u;
    if(mant) bits |= 0x400000;
    return tn_f32_from_bits(bits);
  }
  float pow2 = 1.0f;
  int shift = (int)exp - (e5m2 ? 15 : 7);
  if(shift >= 0) while(shift--) pow2 *= 2.0f;
  else while(shift++) pow2 /= 2.0f;
  return sign * (1.0f + (float)mant / (float)(1u << mant_bits)) * pow2;
}

static inline uint64_t tn_float_bits_from_double(double x, TyKind kind){
  switch(kind){
    case TY_F64: {
      uint64_t bits;
      memcpy(&bits, &x, sizeof(bits));
      return bits;
    }
    case TY_F32: return tn_f32_bits((float)x);
    case TY_F16: return tn_f32_to_f16((float)x);
    case TY_BF16: return tn_f32_to_bf16((float)x);
    case TY_F8_E4M3: return tn_f32_to_f8((float)x, 0);
    case TY_F8_E5M2: return tn_f32_to_f8((float)x, 1);
    default: return 0;
  }
}

static inline double tn_float_bits_to_double(uint64_t bits, TyKind kind){
  switch(kind){
    case TY_F64: {
      double x;
      memcpy(&x, &bits, sizeof(x));
      return x;
    }
    case TY_F32: return tn_f32_from_bits((uint32_t)bits);
    case TY_F16: return tn_f16_to_f32((uint16_t)bits);
    case TY_BF16: return tn_f32_from_bits((uint32_t)(uint16_t)bits << 16);
    case TY_F8_E4M3: return tn_f8_to_f32((uint8_t)bits, 0);
    case TY_F8_E5M2: return tn_f8_to_f32((uint8_t)bits, 1);
    default: return 0.0;
  }
}
