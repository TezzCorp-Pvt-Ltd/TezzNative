#pragma once
#include "vm.h"

int tn_vm_native_call(const char* name, int len, Value* args, int argc, Value* out);
