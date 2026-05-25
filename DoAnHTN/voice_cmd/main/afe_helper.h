#pragma once

#include "esp_afe_sr_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

const esp_afe_sr_iface_t *get_afe_handle(void);
esp_afe_sr_data_t *create_afe_data(void);

#ifdef __cplusplus
}
#endif