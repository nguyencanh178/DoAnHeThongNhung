idf_component_register(
    SRCS
        "voice_cmd.cc"
        "afe_helper.c"
        "mel_feature.cc"
        "model_data.cc"
        "stage1_model.cc"
        "stage2_model.cc"

    INCLUDE_DIRS
        "."

    REQUIRES
        driver
        esp_timer
        esp-dsp
        esp-tflite-micro
        esp-sr
        dht
        ds3231
        st7789
)