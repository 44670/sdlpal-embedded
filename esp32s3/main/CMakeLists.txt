idf_component_register(
    SRCS
        "app_main.c"
        "cores3se_board.c"
        "cores3se_memory.c"
        "../../embedded/pal_pack.c"
        "../../embedded/pal_video_static.c"
    INCLUDE_DIRS
        "."
        "../../embedded"
    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_spi
        esp_lcd
        esp_partition
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    PAL_ESP_CORES3SE=1
)
