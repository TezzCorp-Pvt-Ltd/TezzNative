#pragma once
#include <string.h>

static inline int tn_is_hardware_target(const char* target){
  return target && (!strcmp(target,"arduino-uno") || !strcmp(target,"raspi3b") ||
                    !strcmp(target,"pc-x86_64") || !strcmp(target,"riscv64-virt"));
}
