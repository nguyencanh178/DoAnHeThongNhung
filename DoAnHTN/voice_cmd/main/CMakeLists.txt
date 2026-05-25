idf_component_register(
    SRCS
        "voice_cmd.cc"
        "afe_helper.c"
        "mel_feature.cc"
        "stage1_model.cc"
        "stage2_model.cc"

    INCLUDE_DIRS "."

    REQUIRES
        driver
        esp-sr
        esp-dsp
        esp-tflite-micro
)