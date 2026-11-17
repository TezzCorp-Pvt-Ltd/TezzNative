#pragma once
#include "ir.h"
#include "util.h"
#include <stdio.h>

/* Embed helpers so generated files require no SDK headers at compile time. */
static void emit_integer_prelude(FILE* file,int cuda){
  fputs(cuda ? "#define TN_INTEGER_FN static __host__ __device__ __forceinline__\n"
             : "#define TN_INTEGER_FN static inline\nextern void abort(void);\n",file);
  fputs(
    "TN_INTEGER_FN int64_t tn_ir_i64_bits(uint64_t x){ return x<=INT64_MAX ? (int64_t)x : -1-(int64_t)(UINT64_MAX-x); }\n"
    "TN_INTEGER_FN int64_t tn_ir_i64_add(int64_t a,int64_t b){ return tn_ir_i64_bits((uint64_t)a+(uint64_t)b); }\n"
    "TN_INTEGER_FN int64_t tn_ir_i64_sub(int64_t a,int64_t b){ return tn_ir_i64_bits((uint64_t)a-(uint64_t)b); }\n"
    "TN_INTEGER_FN int64_t tn_ir_i64_mul(int64_t a,int64_t b){ return tn_ir_i64_bits((uint64_t)a*(uint64_t)b); }\n"
    "TN_INTEGER_FN int64_t tn_ir_i64_and(int64_t a,int64_t b){ return tn_ir_i64_bits((uint64_t)a&(uint64_t)b); }\n"
    "TN_INTEGER_FN int64_t tn_ir_i64_or(int64_t a,int64_t b){ return tn_ir_i64_bits((uint64_t)a|(uint64_t)b); }\n"
    "TN_INTEGER_FN int64_t tn_ir_i64_xor(int64_t a,int64_t b){ return tn_ir_i64_bits((uint64_t)a^(uint64_t)b); }\n"
    "TN_INTEGER_FN int64_t tn_ir_i64_shl(int64_t a,int64_t b){ return tn_ir_i64_bits((uint64_t)a<<((uint64_t)b&63)); }\n"
    "TN_INTEGER_FN int64_t tn_ir_i64_shr(int64_t a,int64_t b){ unsigned c=(unsigned)((uint64_t)b&63); uint64_t r=(uint64_t)a>>c; if(a<0&&c)r|=UINT64_MAX<<(64-c); return tn_ir_i64_bits(r); }\n"
    "TN_INTEGER_FN void tn_ir_i64_failure(void){\n"
    "#if defined(__CUDA_ARCH__)\n"
    "  asm volatile(\"trap;\");\n"
    "#else\n"
    "  abort();\n"
    "#endif\n"
    "}\n"
    "TN_INTEGER_FN int64_t tn_ir_i64_div(int64_t a,int64_t b){ if(!b||(a==INT64_MIN&&b==-1)){tn_ir_i64_failure();return a;} return a/b; }\n"
    "TN_INTEGER_FN int64_t tn_ir_i64_mod(int64_t a,int64_t b){ if(!b||(a==INT64_MIN&&b==-1)){tn_ir_i64_failure();return a;} return a%b; }\n\n",file);
}

static void emit_integer_binary(FILE* file,const IRIns* instruction){
  static const char* names[]={"add","sub","mul","div","mod","and","or","xor","shl","shr"};
  if(instruction->binop<B_ADD || instruction->binop>B_SHR) die("unsupported integer IR operation");
  fprintf(file,"  r[%d] = tn_ir_i64_%s(r[%d],r[%d]);\n",instruction->a,names[instruction->binop],instruction->b,instruction->c);
}
