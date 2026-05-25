#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "afe_helper.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_afe_config.h"

const esp_afe_sr_iface_t *get_afe_handle(void)
{
    return &ESP_AFE_SR_HANDLE;
}

esp_afe_sr_data_t *create_afe_data(void)
{
    afe_config_t afe_config = AFE_CONFIG_DEFAULT();

    afe_config.aec_init = false;
    afe_config.se_init = false;
    afe_config.vad_init = false;
    afe_config.wakenet_init = true;

    afe_config.wakenet_model_name = "wn9_hiesp";

    afe_config.pcm_config.total_ch_num = 1;
    afe_config.pcm_config.mic_num = 1;
    afe_config.pcm_config.ref_num = 0;
    afe_config.pcm_config.sample_rate = 16000;

    afe_config.memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    afe_config.agc_mode = AFE_MN_PEAK_AGC_MODE_2;
    afe_config.afe_mode = SR_MODE_LOW_COST;

    return ESP_AFE_SR_HANDLE.create_from_config(&afe_config);
}