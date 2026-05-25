#include "mel_feature.h"
#include <math.h>
#include <string.h>
#include "esp_dsp.h"

#define SAMPLE_RATE 16000
#define AUDIO_SAMPLES 32000

#define FFT_LEN 512
#define WIN_LEN 400
#define HOP_LEN 160
#define FFT_BINS 257

#define FMIN 80.0f
#define FMAX 7600.0f

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static float fft_buffer[FFT_LEN * 2];
static float window_table[WIN_LEN];
static float mel_weights[FFT_BINS][MEL_BINS];

static bool ready = false;

static float hz_to_mel(float hz)
{
    return 2595.0f * log10f(1.0f + hz / 700.0f);
}

static float mel_to_hz(float mel)
{
    return 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f);
}

static void init_feature()
{
    if (ready) return;

    dsps_fft2r_init_fc32(NULL, FFT_LEN);

    for (int i = 0; i < WIN_LEN; i++) {
        window_table[i] = 0.5f - 0.5f * cosf(2.0f * M_PI * i / (WIN_LEN - 1));
    }

    memset(mel_weights, 0, sizeof(mel_weights));

    float lower_mel = hz_to_mel(FMIN);
    float upper_mel = hz_to_mel(FMAX);

    float mel_points[MEL_BINS + 2];
    float hz_points[MEL_BINS + 2];
    int bin[MEL_BINS + 2];

    for (int i = 0; i < MEL_BINS + 2; i++) {
        mel_points[i] = lower_mel + (upper_mel - lower_mel) * i / (MEL_BINS + 1);
        hz_points[i] = mel_to_hz(mel_points[i]);

        bin[i] = (int)floorf((FFT_LEN + 1) * hz_points[i] / SAMPLE_RATE);

        if (bin[i] < 0) bin[i] = 0;
        if (bin[i] > FFT_BINS - 1) bin[i] = FFT_BINS - 1;
    }

    for (int m = 1; m <= MEL_BINS; m++) {
        int left = bin[m - 1];
        int center = bin[m];
        int right = bin[m + 1];

        for (int k = left; k < center; k++) {
            if (center != left) {
                mel_weights[k][m - 1] = (float)(k - left) / (center - left);
            }
        }

        for (int k = center; k < right; k++) {
            if (right != center) {
                mel_weights[k][m - 1] = (float)(right - k) / (right - center);
            }
        }
    }

    ready = true;
}

void audio_to_logmel(const int16_t *audio, float *out_feature)
{
    init_feature();

    float power[FFT_BINS];
    float max_mel = 1e-12f;

    for (int frame = 0; frame < MEL_FRAMES; frame++) {
        memset(fft_buffer, 0, sizeof(fft_buffer));

        int frame_start = frame * HOP_LEN - FFT_LEN / 2;
        int win_offset = (FFT_LEN - WIN_LEN) / 2;

        for (int i = 0; i < WIN_LEN; i++) {
            int audio_idx = frame_start + i;
            float x = 0.0f;

            if (audio_idx >= 0 && audio_idx < AUDIO_SAMPLES) {
                x = (float)audio[audio_idx] / 32768.0f;
            }

            x *= window_table[i];

            int fft_idx = win_offset + i;
            fft_buffer[2 * fft_idx] = x;
            fft_buffer[2 * fft_idx + 1] = 0.0f;
        }

        dsps_fft2r_fc32(fft_buffer, FFT_LEN);
        dsps_bit_rev_fc32(fft_buffer, FFT_LEN);
        dsps_cplx2reC_fc32(fft_buffer, FFT_LEN);

        power[0] = fft_buffer[0] * fft_buffer[0];

        for (int k = 1; k < FFT_BINS - 1; k++) {
            float real = fft_buffer[2 * k];
            float imag = fft_buffer[2 * k + 1];
            power[k] = real * real + imag * imag;
        }

        power[FFT_BINS - 1] = fft_buffer[1] * fft_buffer[1];

        for (int m = 0; m < MEL_BINS; m++) {
            float mel_energy = 0.0f;

            for (int k = 0; k < FFT_BINS; k++) {
                mel_energy += power[k] * mel_weights[k][m];
            }

            if (mel_energy < 1e-12f) mel_energy = 1e-12f;
            if (mel_energy > max_mel) max_mel = mel_energy;

            out_feature[frame * MEL_BINS + m] = mel_energy;
        }
    }

    float mean = 0.0f;
    float std = 0.0f;

    for (int i = 0; i < MODEL_INPUT_SIZE; i++) {
        float db = 10.0f * log10f(out_feature[i] / max_mel);
        out_feature[i] = db;
        mean += db;
    }

    mean /= MODEL_INPUT_SIZE;

    for (int i = 0; i < MODEL_INPUT_SIZE; i++) {
        float d = out_feature[i] - mean;
        std += d * d;
    }

    std = sqrtf(std / MODEL_INPUT_SIZE) + 1e-6f;

    for (int i = 0; i < MODEL_INPUT_SIZE; i++) {
        out_feature[i] = (out_feature[i] - mean) / std;
    }
}