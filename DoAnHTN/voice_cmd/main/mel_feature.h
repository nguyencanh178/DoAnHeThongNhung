#pragma once

#include <stdint.h>

#define MEL_FRAMES 201
#define MEL_BINS 40
#define MODEL_INPUT_SIZE (MEL_FRAMES * MEL_BINS)

void audio_to_logmel(const int16_t *audio, float *out_feature);