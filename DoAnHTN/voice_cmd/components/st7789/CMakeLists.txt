idf_component_register(
    SRCS "st7789.c"
    INCLUDE_DIRS "."
    REQUIRES driver freertos esp_common log
)