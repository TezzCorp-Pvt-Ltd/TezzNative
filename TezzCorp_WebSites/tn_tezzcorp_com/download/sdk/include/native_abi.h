#pragma once

#ifdef _WIN32
#define TN_NATIVE_API __declspec(dllexport)
#elif defined(TN_NATIVE_X64_ABI) && defined(__x86_64__)
// Generated x64 code uses the Win64 calling convention on every host.
#define TN_NATIVE_API __attribute__((ms_abi, visibility("default")))
#else
#define TN_NATIVE_API __attribute__((visibility("default")))
#endif
