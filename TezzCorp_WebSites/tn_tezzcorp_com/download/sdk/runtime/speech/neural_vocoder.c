#include "speech.h"
#include "neural_vocoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define NVOC_MELS 80
#define NVOC_INITIAL_CHANNELS 128
#define NVOC_NUM_UPS 4
#define NVOC_NUM_RES 8

typedef struct {
    int in_channels;
    int out_channels;
    int kernel_size;
    int dilation;
    int padding;
    float* weight;
    float* bias;
} NVConv1D;

typedef struct {
    int in_channels;
    int out_channels;
    int kernel_size;
    int stride;
    int padding;
    float* weight;
    float* bias;
} NVConvTranspose1D;

typedef struct {
    int channels;
    int kernel_size;
    NVConv1D convs1[3];
    NVConv1D convs2[3];
} NVResBlock;

typedef struct {
    int sample_rate;
    int is_loaded;
    NVConv1D conv_pre;
    NVConvTranspose1D ups[4];
    NVResBlock resblocks[8];
    NVConv1D conv_post;
} NeuralVocoder;

static NeuralVocoder g_vocoder = {0};

static inline void leaky_relu(float* data, size_t count, float slope) {
    for (size_t i = 0; i < count; i++) {
        if (data[i] < 0.0f) data[i] *= slope;
    }
}

static void conv1d_forward(const float* input, int in_channels, int in_len,
                           const NVConv1D* conv, float* output) {
    int out_channels = conv->out_channels;
    int kernel_size = conv->kernel_size;
    int dilation = conv->dilation;
    int padding = conv->padding;
    const float* weight = conv->weight;
    const float* bias = conv->bias;

    for (int co = 0; co < out_channels; co++) {
        float b = bias ? bias[co] : 0.0f;
        const float* w_co = &weight[co * in_channels * kernel_size];
        float* out_co = &output[co * in_len];

        for (int t = 0; t < in_len; t++) {
            float sum = b;
            for (int ci = 0; ci < in_channels; ci++) {
                const float* in_ci = &input[ci * in_len];
                const float* w_ci = &w_co[ci * kernel_size];
                for (int k = 0; k < kernel_size; k++) {
                    int in_idx = t - padding + k * dilation;
                    if (in_idx >= 0 && in_idx < in_len) {
                        sum += in_ci[in_idx] * w_ci[k];
                    }
                }
            }
            out_co[t] = sum;
        }
    }
}

static void conv_transpose1d_forward(const float* input, int in_channels, int in_len,
                                     const NVConvTranspose1D* conv, float* output, int out_len) {
    int out_channels = conv->out_channels;
    int kernel_size = conv->kernel_size;
    int stride = conv->stride;
    int padding = conv->padding;
    const float* weight = conv->weight;
    const float* bias = conv->bias;

    for (int co = 0; co < out_channels; co++) {
        float b = bias ? bias[co] : 0.0f;
        float* out_co = &output[co * out_len];
        for (int t = 0; t < out_len; t++) {
            out_co[t] = b;
        }
    }

    for (int ci = 0; ci < in_channels; ci++) {
        const float* in_ci = &input[ci * in_len];
        const float* w_ci = &weight[ci * out_channels * kernel_size];

        for (int ti = 0; ti < in_len; ti++) {
            float in_val = in_ci[ti];
            if (fabsf(in_val) < 1e-12f) continue;

            for (int co = 0; co < out_channels; co++) {
                const float* w_co = &w_ci[co * kernel_size];
                float* out_co = &output[co * out_len];

                for (int k = 0; k < kernel_size; k++) {
                    int to = ti * stride - padding + k;
                    if (to >= 0 && to < out_len) {
                        out_co[to] += in_val * w_co[k];
                    }
                }
            }
        }
    }
}

static void resblock_forward(const float* input, int channels, int len,
                             const NVResBlock* rb, float* output,
                             float* temp1, float* temp2) {
    memcpy(output, input, (size_t)channels * len * sizeof(float));

    for (int d = 0; d < 3; d++) {
        memcpy(temp1, output, (size_t)channels * len * sizeof(float));

        leaky_relu(output, (size_t)channels * len, 0.1f);
        conv1d_forward(output, channels, len, &rb->convs1[d], temp2);

        leaky_relu(temp2, (size_t)channels * len, 0.1f);
        conv1d_forward(temp2, channels, len, &rb->convs2[d], output);

        for (size_t i = 0; i < (size_t)channels * len; i++) {
            output[i] += temp1[i];
        }
    }
}

int speech_neural_vocoder_is_loaded(void) {
    return g_vocoder.is_loaded;
}

void speech_neural_vocoder_free(void) {
    if (!g_vocoder.is_loaded) return;

    free(g_vocoder.conv_pre.weight); free(g_vocoder.conv_pre.bias);
    for (int i = 0; i < 4; i++) {
        free(g_vocoder.ups[i].weight); free(g_vocoder.ups[i].bias);
    }
    for (int i = 0; i < 8; i++) {
        for (int d = 0; d < 3; d++) {
            free(g_vocoder.resblocks[i].convs1[d].weight);
            free(g_vocoder.resblocks[i].convs1[d].bias);
            free(g_vocoder.resblocks[i].convs2[d].weight);
            free(g_vocoder.resblocks[i].convs2[d].bias);
        }
    }
    free(g_vocoder.conv_post.weight); free(g_vocoder.conv_post.bias);

    memset(&g_vocoder, 0, sizeof(NeuralVocoder));
}

static void init_layer_meta(void) {
    g_vocoder.sample_rate = 16000;

    // conv_pre: [128, 80, 7]
    g_vocoder.conv_pre.in_channels = 80;
    g_vocoder.conv_pre.out_channels = 128;
    g_vocoder.conv_pre.kernel_size = 7;
    g_vocoder.conv_pre.dilation = 1;
    g_vocoder.conv_pre.padding = 3;

    // ups: rates=(8, 5, 2, 2), kernels=(16, 11, 4, 4)
    int up_rates[4] = {8, 5, 2, 2};
    int up_kernels[4] = {16, 11, 4, 4};
    int ch_in = 128;
    for (int i = 0; i < 4; i++) {
        int ch_out = ch_in / 2;
        g_vocoder.ups[i].in_channels = ch_in;
        g_vocoder.ups[i].out_channels = ch_out;
        g_vocoder.ups[i].kernel_size = up_kernels[i];
        g_vocoder.ups[i].stride = up_rates[i];
        g_vocoder.ups[i].padding = (up_kernels[i] - up_rates[i]) / 2;
        ch_in = ch_out;
    }

    // resblocks: 2 per up stage (kernel 3 and 7), dilations 1, 3, 5
    int dilations[3] = {1, 3, 5};
    int res_channels[4] = {64, 32, 16, 8};
    for (int u = 0; u < 4; u++) {
        int ch = res_channels[u];
        for (int r = 0; r < 2; r++) {
            int rb_idx = u * 2 + r;
            int k = (r == 0) ? 3 : 7;
            g_vocoder.resblocks[rb_idx].channels = ch;
            g_vocoder.resblocks[rb_idx].kernel_size = k;

            for (int d = 0; d < 3; d++) {
                // convs1
                g_vocoder.resblocks[rb_idx].convs1[d].in_channels = ch;
                g_vocoder.resblocks[rb_idx].convs1[d].out_channels = ch;
                g_vocoder.resblocks[rb_idx].convs1[d].kernel_size = k;
                g_vocoder.resblocks[rb_idx].convs1[d].dilation = dilations[d];
                g_vocoder.resblocks[rb_idx].convs1[d].padding = (k * dilations[d] - dilations[d]) / 2;

                // convs2
                g_vocoder.resblocks[rb_idx].convs2[d].in_channels = ch;
                g_vocoder.resblocks[rb_idx].convs2[d].out_channels = ch;
                g_vocoder.resblocks[rb_idx].convs2[d].kernel_size = k;
                g_vocoder.resblocks[rb_idx].convs2[d].dilation = 1;
                g_vocoder.resblocks[rb_idx].convs2[d].padding = (k - 1) / 2;
            }
        }
    }

    // conv_post: [1, 8, 7]
    g_vocoder.conv_post.in_channels = 8;
    g_vocoder.conv_post.out_channels = 1;
    g_vocoder.conv_post.kernel_size = 7;
    g_vocoder.conv_post.dilation = 1;
    g_vocoder.conv_post.padding = 3;
}

int speech_neural_vocoder_load(const char* weights_path) {
    if (!weights_path) return -1;
    FILE* f = fopen(weights_path, "rb");
    if (!f) return -1;

    speech_neural_vocoder_free();
    init_layer_meta();

    char magic[8];
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "TNVOC01\0", 8) != 0) {
        fclose(f);
        return -1;
    }

    int config[8];
    if (fread(config, sizeof(int), 8, f) != 8) {
        fclose(f);
        return -1;
    }

    while (!feof(f)) {
        int name_len = 0;
        if (fread(&name_len, sizeof(int), 1, f) != 1) break;
        if (name_len <= 0 || name_len > 256) { fclose(f); speech_neural_vocoder_free(); return -1; }

        char name[256];
        if (fread(name, 1, name_len, f) != (size_t)name_len) { fclose(f); speech_neural_vocoder_free(); return -1; }
        name[name_len] = '\0';

        int num_dims = 0;
        if (fread(&num_dims, sizeof(int), 1, f) != 1) { fclose(f); speech_neural_vocoder_free(); return -1; }
        int dims[8] = {0};
        if (fread(dims, sizeof(int), num_dims, f) != (size_t)num_dims) { fclose(f); speech_neural_vocoder_free(); return -1; }

        int data_bytes = 0;
        if (fread(&data_bytes, sizeof(int), 1, f) != 1) { fclose(f); speech_neural_vocoder_free(); return -1; }
        float* data = (float*)malloc((size_t)data_bytes);
        if (!data || fread(data, 1, data_bytes, f) != (size_t)data_bytes) {
            free(data); fclose(f); speech_neural_vocoder_free(); return -1;
        }

        // Match parameter name
        if (strcmp(name, "conv_pre.weight") == 0) {
            g_vocoder.conv_pre.weight = data;
        } else if (strcmp(name, "conv_pre.bias") == 0) {
            g_vocoder.conv_pre.bias = data;
        } else if (strcmp(name, "conv_post.weight") == 0) {
            g_vocoder.conv_post.weight = data;
        } else if (strcmp(name, "conv_post.bias") == 0) {
            g_vocoder.conv_post.bias = data;
        } else if (strncmp(name, "ups.", 4) == 0) {
            int u = name[4] - '0';
            if (u >= 0 && u < 4) {
                if (strstr(name, ".weight")) g_vocoder.ups[u].weight = data;
                else if (strstr(name, ".bias")) g_vocoder.ups[u].bias = data;
                else free(data);
            } else free(data);
        } else if (strncmp(name, "resblocks.", 10) == 0) {
            int rb = name[10] - '0';
            if (rb >= 0 && rb < 8) {
                int d = -1;
                char* convs1 = strstr(name, "convs1.");
                char* convs2 = strstr(name, "convs2.");
                if (convs1) {
                    d = convs1[7] - '0';
                    if (d >= 0 && d < 3) {
                        if (strstr(name, ".weight")) g_vocoder.resblocks[rb].convs1[d].weight = data;
                        else if (strstr(name, ".bias")) g_vocoder.resblocks[rb].convs1[d].bias = data;
                        else free(data);
                    } else free(data);
                } else if (convs2) {
                    d = convs2[7] - '0';
                    if (d >= 0 && d < 3) {
                        if (strstr(name, ".weight")) g_vocoder.resblocks[rb].convs2[d].weight = data;
                        else if (strstr(name, ".bias")) g_vocoder.resblocks[rb].convs2[d].bias = data;
                        else free(data);
                    } else free(data);
                } else free(data);
            } else free(data);
        } else {
            free(data);
        }
    }

    fclose(f);
    g_vocoder.is_loaded = 1;
    return 0;
}

int speech_neural_vocode(const double* mel, int frames, int64_t* pcm, int capacity, double pitch, double energy) {
    if (!g_vocoder.is_loaded || !mel || !pcm || frames < 1 || frames > TN_SPEECH_MAX_FRAMES) return -1;

    int total_samples = frames * 160;
    if (total_samples > capacity) return -1;

    // Buffer allocation
    int max_len = total_samples;
    float* buf_in = (float*)malloc((size_t)80 * frames * sizeof(float));
    float* buf_a = (float*)malloc((size_t)128 * max_len * sizeof(float));
    float* buf_b = (float*)malloc((size_t)128 * max_len * sizeof(float));
    float* buf_rb0 = (float*)malloc((size_t)64 * max_len * sizeof(float));
    float* buf_rb1 = (float*)malloc((size_t)64 * max_len * sizeof(float));
    float* temp1 = (float*)malloc((size_t)64 * max_len * sizeof(float));
    float* temp2 = (float*)malloc((size_t)64 * max_len * sizeof(float));
    float* buf_out = (float*)malloc((size_t)max_len * sizeof(float));

    if (!buf_in || !buf_a || !buf_b || !buf_rb0 || !buf_rb1 || !temp1 || !temp2 || !buf_out) {
        free(buf_in); free(buf_a); free(buf_b); free(buf_rb0); free(buf_rb1);
        free(temp1); free(temp2); free(buf_out);
        return -1;
    }

    // 1. Transpose mel [frames, 80] -> [80, frames]
    for (int t = 0; t < frames; t++) {
        for (int m = 0; m < 80; m++) {
            buf_in[m * frames + t] = (float)mel[t * 80 + m];
        }
    }

    // 2. conv_pre: [80, frames] -> [128, frames]
    conv1d_forward(buf_in, 80, frames, &g_vocoder.conv_pre, buf_a);

    // 3. Upsampling stages with ping-pong buffers
    int cur_len = frames;
    int cur_ch = 128;

    for (int u = 0; u < 4; u++) {
        float* in_buf = (u % 2 == 0) ? buf_a : buf_b;
        float* out_buf = (u % 2 == 0) ? buf_b : buf_a;

        leaky_relu(in_buf, (size_t)cur_ch * cur_len, 0.1f);

        int next_len = cur_len * g_vocoder.ups[u].stride;
        int next_ch = g_vocoder.ups[u].out_channels;

        conv_transpose1d_forward(in_buf, cur_ch, cur_len, &g_vocoder.ups[u], out_buf, next_len);

        // ResBlock 0
        resblock_forward(out_buf, next_ch, next_len, &g_vocoder.resblocks[u * 2 + 0], buf_rb0, temp1, temp2);

        // ResBlock 1
        resblock_forward(out_buf, next_ch, next_len, &g_vocoder.resblocks[u * 2 + 1], buf_rb1, temp1, temp2);

        // Average ResBlocks back into out_buf
        for (size_t i = 0; i < (size_t)next_ch * next_len; i++) {
            out_buf[i] = (buf_rb0[i] + buf_rb1[i]) * 0.5f;
        }

        cur_len = next_len;
        cur_ch = next_ch;
    }

    // After 4 stages (u=0->B, u=1->A, u=2->B, u=3->A), final activations are in buf_a
    float* final_act = buf_a;

    // 4. conv_post: [8, cur_len] -> [1, cur_len]
    leaky_relu(final_act, (size_t)cur_ch * cur_len, 0.01f);
    conv1d_forward(final_act, 8, cur_len, &g_vocoder.conv_post, buf_out);

    // 5. Tanh + PCM scaling
    double eff_energy = (energy > 0.0) ? energy : 1.0;
    for (int i = 0; i < cur_len; i++) {
        float sample_f = tanhf(buf_out[i]) * (float)(32767.0 * eff_energy);
        if (sample_f > 32767.0f) sample_f = 32767.0f;
        if (sample_f < -32768.0f) sample_f = -32768.0f;
        pcm[i] = (int64_t)llroundf(sample_f);
    }

    free(buf_in); free(buf_a); free(buf_b); free(buf_rb0); free(buf_rb1);
    free(temp1); free(temp2); free(buf_out);

    return cur_len;
}
