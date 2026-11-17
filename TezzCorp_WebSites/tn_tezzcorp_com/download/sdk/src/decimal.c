#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "native_abi.h"
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

#define TN_DECIMAL_MAX_INPUT (1024 * 1024)
_Static_assert(sizeof(double)==sizeof(uint64_t), "decimal conversion requires binary64 storage");

/* A bounded JSON decimal span, converted without changing process locale.
 * Finite underflow (including signed zero) is valid. Overflow is rejected.
 * The caller's output remains untouched on any failure. */
TN_NATIVE_API long long tn_decimal_f64(const unsigned char* text, long long length,
                                      double* output){
  if(!text || !output || length <= 0 || length > TN_DECIMAL_MAX_INPUT) return -1;
  size_t size=(size_t)length, i=0;
  if(text[i]=='-') i++;
  size_t digits_start=i;
  if(i>=size) return -1;
  if(text[i]=='0'){
    i++;
    if(i<size && text[i]>='0' && text[i]<='9') return -1;
  }else{
    if(text[i]<'1' || text[i]>'9') return -1;
    do { i++; } while(i<size && text[i]>='0' && text[i]<='9');
  }
  size_t integer_end=i;
  if(i<size && text[i]=='.'){
    i++;
    size_t start=i;
    while(i<size && text[i]>='0' && text[i]<='9') i++;
    if(i==start) return -1;
  }
  size_t significand_end=i;
  long long exponent=0, exponent_sign=1;
  if(i<size && (text[i]=='e' || text[i]=='E')){
    i++;
    if(i<size && (text[i]=='+' || text[i]=='-')){
      if(text[i]=='-') exponent_sign=-1;
      i++;
    }
    size_t start=i;
    long long limit=length+4096;
    while(i<size && text[i]>='0' && text[i]<='9'){
      if(exponent<limit){
        exponent=exponent*10+(text[i]-'0');
        if(exponent>limit) exponent=limit;
      }
      i++;
    }
    if(i==start) return -1;
  }
  if(i!=size) return -1;
  size_t position=0, first=size;
  for(size_t j=digits_start;j<significand_end;j++){
    if(text[j]=='.') continue;
    if(first==size && text[j]!='0') first=position;
    position++;
  }
  uint64_t zero_bits=text[0]=='-' ? UINT64_C(0x8000000000000000) : 0;
  if(first==size){ memcpy(output,&zero_bits,sizeof(zero_bits)); return 0; }
  exponent=exponent_sign*exponent+(long long)(integer_end-digits_start)-(long long)first-1;
  if(exponent>4096) return -1;
  if(exponent<-4096){ memcpy(output,&zero_bits,sizeof(zero_bits)); return 0; }
  // Some Windows CRTs overflow a long integer mantissa before applying a
  // compensating exponent. Normalize lexically while retaining every digit.
  char* copy=(char*)malloc(size+64);
  if(!copy) return -1;
  size_t written=0;
  if(text[0]=='-') copy[written++]='-';
  position=0;
  int emitted=0;
  for(size_t j=digits_start;j<significand_end;j++){
    if(text[j]=='.') continue;
    if(position++<first) continue;
    copy[written++]=(char)text[j];
    if(!emitted){ copy[written++]='.'; emitted=1; }
  }
  if(copy[written-1]=='.') copy[written++]='0';
  int suffix=snprintf(copy+written,size+64-written,"e%lld",exponent);
  if(suffix<=0 || (size_t)suffix>=size+64-written){ free(copy); return -1; }
  written+=(size_t)suffix;
  char* end=NULL;
  double value;
#ifdef _WIN32
  // The legacy MSVCRT used by some MinGW distributions rounds halfway
  // decimals incorrectly. Supported Windows versions ship the modern UCRT.
  // Obtain all locale functions from that same runtime; locale objects from
  // different CRTs are incompatible.
  HMODULE crt=LoadLibraryExW(L"ucrtbase.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);
  if(!crt){ free(copy); return -1; }
  typedef void* (__cdecl *CreateLocale)(int,const char*);
  typedef double (__cdecl *ParseDecimal)(const char*,char**,void*);
  typedef void (__cdecl *FreeLocale)(void*);
  CreateLocale create_locale=(CreateLocale)GetProcAddress(crt,"_create_locale");
  ParseDecimal parse_decimal=(ParseDecimal)GetProcAddress(crt,"_strtod_l");
  FreeLocale free_locale=(FreeLocale)GetProcAddress(crt,"_free_locale");
  if(!create_locale || !parse_decimal || !free_locale){
    FreeLibrary(crt); free(copy); return -1;
  }
  void* numeric=create_locale(LC_NUMERIC,"C");
  if(!numeric){ FreeLibrary(crt); free(copy); return -1; }
  value=parse_decimal(copy,&end,numeric);
  free_locale(numeric);
  FreeLibrary(crt);
#else
  locale_t numeric=newlocale(LC_NUMERIC_MASK,"C",(locale_t)0);
  if(!numeric){ free(copy); return -1; }
  value=strtod_l(copy,&end,numeric);
  freelocale(numeric);
#endif
  int complete=end==copy+written;
  uint64_t bits=0;
  memcpy(&bits,&value,sizeof(bits));
  free(copy);
  if(!complete || (bits & UINT64_C(0x7ff0000000000000))==UINT64_C(0x7ff0000000000000)) return -1;
  if((bits & UINT64_C(0x7fffffffffffffff))==0) bits=zero_bits;
  memcpy(output,&bits,sizeof(bits));
  return 0;
}

