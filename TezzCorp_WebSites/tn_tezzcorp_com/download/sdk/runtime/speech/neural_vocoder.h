#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns 0 on success, -1 on failure
int speech_neural_vocoder_load(const char* weights_path);

// Unloads weights and frees memory
void speech_neural_vocoder_free(void);

// Returns 1 if loaded, 0 otherwise
int speech_neural_vocoder_is_loaded(void);

// Synthesize 16-bit PCM waveform from mel spectrogram
// mel: [frames * 80] row-major (mel[t * 80 + m])
// frames: number of mel frames
// pcm: output buffer (at least frames * 160 samples)
// capacity: capacity of pcm buffer
// pitch: pitch multiplier (1.0 = normal)
// energy: energy multiplier (1.0 = normal)
// returns number of samples written, or -1 on error
int speech_neural_vocode(const double* mel, int frames, int64_t* pcm, int capacity, double pitch, double energy);

#ifdef __cplusplus
}
#endif
