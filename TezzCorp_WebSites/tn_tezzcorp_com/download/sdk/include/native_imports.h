#pragma once
#include "ir.h"
#include <stdint.h>
#include <stddef.h>

#define TN_NATIVE_IMPORT_MAX 256
enum { TN_NATIVE_RUNTIME, TN_NATIVE_CPU, TN_NATIVE_GPU, TN_NATIVE_LIBRARY_COUNT };
typedef struct {
  char symbol[128];
  int library;
  uint32_t iat;
} TnNativeImport;
typedef struct {
  TnNativeImport entries[TN_NATIVE_IMPORT_MAX];
  int count;
} TnNativeImports;

int tn_native_collect(IRModule* module, TnNativeImports* imports);
int tn_native_find(const TnNativeImports* imports, const char* name, int len);
const char* tn_native_library_name(int library, int windows);
int tn_native_library_path(const char* name, char* path, size_t cap);
int tn_native_deploy(const TnNativeImports* imports, const char* output, int windows, int machine);
