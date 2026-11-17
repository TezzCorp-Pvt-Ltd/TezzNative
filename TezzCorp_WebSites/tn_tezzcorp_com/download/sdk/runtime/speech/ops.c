/* Generic CTC primitives for models composed in TezzNative. No model state. */
#include "speech.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

/* Generic UTF-8 file primitives. Serialization and temporary-file ownership
 * stay in the calling TN module. Exclusive creation never truncates a stale
 * temporary file; replacement happens only after the writer closes it. */
static int speech_file_path_valid(const char* path){
  if(!path || !path[0]) return 0;
  for(size_t i = 0; i <= 4100; ++i) if(!path[i]) return 1;
  return 0;
}
#ifdef _WIN32
static wchar_t* speech_file_wide(const char* path){
  if(!speech_file_path_valid(path)) return NULL;
  int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
  if(count < 1) return NULL;
  wchar_t* wide = malloc((size_t)count * sizeof(wchar_t));
  if(!wide) return NULL;
  if(!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, count)){
    free(wide); return NULL;
  }
  return wide;
}
#endif

TN_NATIVE_API unsigned char* tn_speech_file_open(const char* path, long long exclusive){
  if(!speech_file_path_valid(path) || exclusive < 0 || exclusive > 1) return NULL;
#ifdef _WIN32
  wchar_t* wide = speech_file_wide(path);
  if(!wide) return NULL;
  if(!exclusive){
    FILE* file = _wfopen(wide, L"rb");
    free(wide); return (unsigned char*)file;
  }
  HANDLE handle = CreateFileW(wide, GENERIC_WRITE, 0, NULL, CREATE_NEW,
    FILE_ATTRIBUTE_NORMAL, NULL);
  if(handle == INVALID_HANDLE_VALUE){ free(wide); return NULL; }
  int descriptor = _open_osfhandle((intptr_t)handle, _O_WRONLY | _O_BINARY);
  if(descriptor < 0){ CloseHandle(handle); DeleteFileW(wide); free(wide); return NULL; }
  FILE* file = _fdopen(descriptor, "wb");
  if(!file){ _close(descriptor); DeleteFileW(wide); }
  free(wide);
#else
  if(!exclusive) return (unsigned char*)fopen(path, "rb");
  int descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if(descriptor < 0) return NULL;
  FILE* file = fdopen(descriptor, "wb");
  if(!file){ close(descriptor); unlink(path); }
#endif
  return (unsigned char*)file;
}

TN_NATIVE_API long long tn_speech_file_replace(const char* source, const char* target){
  if(!speech_file_path_valid(source) || !speech_file_path_valid(target)) return -1;
#ifdef _WIN32
  wchar_t* from = speech_file_wide(source);
  wchar_t* to = speech_file_wide(target);
  int ok = from && to && MoveFileExW(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
  free(from); free(to);
  return ok ? 0 : -1;
#else
  return rename(source, target) == 0 ? 0 : -1;
#endif
}

TN_NATIVE_API long long tn_speech_file_remove(const char* path){
  if(!speech_file_path_valid(path)) return -1;
#ifdef _WIN32
  wchar_t* wide = speech_file_wide(path);
  int ok = wide && DeleteFileW(wide);
  free(wide);
  return ok ? 0 : -1;
#else
  return unlink(path) == 0 ? 0 : -1;
#endif
}

TN_NATIVE_API long long tn_speech_ctc_loss_f32(long long logits, long long frames,
  long long classes, const int64_t* labels, long long count, long long gradient, double* loss){
  if(!logits || !loss || frames < 1 || frames > TN_SPEECH_MAX_FRAMES ||
     classes < 2 || classes > TN_SPEECH_CLASSES || count < 1 ||
     count > TN_SPEECH_MAX_LABELS || !labels) return -1;
  return speech_ctc((const float*)(intptr_t)logits, (int)frames, (int)classes,
    labels, (int)count, (float*)(intptr_t)gradient, NULL, loss);
}

TN_NATIVE_API long long tn_speech_ctc_decode_f32(long long logits, long long frames,
  long long classes, long long width, int64_t* labels, long long capacity){
  if(!logits || !labels || frames < 1 || frames > TN_SPEECH_MAX_FRAMES ||
     classes < 2 || classes > TN_SPEECH_CLASSES || width < 1 || width > 128 ||
     capacity < 1 || capacity > TN_SPEECH_MAX_FRAMES) return -1;
  int* temporary = malloc((size_t)frames * sizeof(int));
  if(!temporary) return -1;
  int count = speech_ctc_decode((const float*)(intptr_t)logits, (int)frames,
    (int)classes, (int)width, temporary, (int)capacity, NULL);
  if(count >= 0) for(int i = 0; i < count; ++i) labels[i] = temporary[i];
  free(temporary);
  return count;
}
