if(NOT PAL_CORES3SE_ENGINE_HOST)
idf_component_register(
    SRCS
        "app_main.c"
        "cores3se_board.c"
        "cores3se_memory.c"
        "pal_save_fatfs.c"
        "../../embedded/pal_battle_cache.c"
        "../../embedded/pal_dialog_static.c"
        "../../embedded/pal_ending_static.c"
        "../../embedded/pal_font_cache.c"
        "../../embedded/pal_global_cache.c"
        "../../embedded/pal_menu_static.c"
        "../../embedded/pal_music_cache.c"
        "../../embedded/pal_pack.c"
        "../../embedded/pal_palette_static.c"
        "../../embedded/pal_rng_cache.c"
        "../../embedded/pal_scene_cache.c"
        "../../embedded/pal_text_cache.c"
        "../../embedded/pal_ui_cache.c"
        "../../embedded/pal_video_static.c"
    INCLUDE_DIRS
        "."
        "../../embedded"
    REQUIRES
        esp_app_format
        esp_driver_gpio
        esp_driver_gptimer
        esp_driver_i2c
        # ESP-IDF's early component-dependency scan does not import the
        # project-only music/board cache options, so it enters this
        # registration branch even for an embedded RIX build. Keep I2S in
        # this discovery superset for both hardware sinks.
        esp_driver_i2s
        esp_driver_ledc
        esp_driver_sdspi
        esp_driver_spi
        esp_lcd
        esp_partition
        esp_timer
        fatfs
        sdmmc
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    PAL_ESP_CORES3SE=1
    PAL_SCENE_ENABLE_PINNED=1
    PAL_ESP_CORES3SE_NO_AUDIO=1
    PAL_ESP_CORES3SE_NO_SFX=1
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -fstack-usage
)
else()
if(XIAOMIAO_WROVER)
    if(NOT CONFIG_IDF_TARGET_ESP32)
        message(FATAL_ERROR "Xiaomiao profile requires target esp32")
    endif()
    if(NOT CONFIG_SPIRAM)
        message(FATAL_ERROR "Xiaomiao profile requires CONFIG_SPIRAM=y")
    endif()
    set(PAL_TARGET_BOARD_SOURCES
        "xiaomiao_board.c"
        "xiaomiao_audio.c"
        "xiaomiao_memory.c"
        "pal_guru_screen.c"
        "cardputer_extreme_native_view.c"
        "../../embedded/pal_level2_resident_pack.c"
        "../../embedded/pal_font10_cache.c"
        "../../embedded/pal_native_ui.c"
        "../engine_bridge/pal_engine_guru.c"
    )
    set(PAL_TARGET_AUDIO_SOURCES
        "../../adplug/player.cpp"
        "../../adplug/rix.cpp"
        "../../embedded/pal_mame_opl2_static.cpp"
        "../../embedded/pal_music_cache.c"
        "../engine_bridge/pal_engine_target_music.cpp"
    )
    set(PAL_ENGINE_LDFRAGMENT_ARGS)
elseif(CARDPUTER_EXTREME_NO_PSRAM)
    if(CONFIG_SPIRAM)
        message(FATAL_ERROR "Cardputer extreme profile must be built with CONFIG_SPIRAM=n")
    endif()
    set(PAL_TARGET_BOARD_SOURCES
        "cardputer_extreme_board.c"
        "cardputer_extreme_memory.c"
        "cardputer_extreme_native_view.c"
        "../../embedded/pal_font10_cache.c"
        "../../embedded/pal_native_ui.c"
    )
    if(CARDPUTER_EXTREME_MUSIC)
        list(APPEND PAL_TARGET_BOARD_SOURCES
            "cardputer_extreme_audio.c"
        )
        set(PAL_TARGET_AUDIO_SOURCES
            "../../adplug/player.cpp"
            "../../adplug/rix.cpp"
            "../../embedded/pal_mame_opl2_static.cpp"
            "../../embedded/pal_music_cache.c"
            "../engine_bridge/pal_engine_target_music.cpp"
        )
    else()
        set(PAL_TARGET_AUDIO_SOURCES
            "../../unix/contract_noaudio.c"
        )
    endif()
    set(PAL_ENGINE_LDFRAGMENT_ARGS)
else()
    set(PAL_TARGET_BOARD_SOURCES
        "cores3se_board.c"
        "cores3se_memory.c"
    )
    set(PAL_ENGINE_LDFRAGMENT_ARGS
        LDFRAGMENTS
            "pal_engine_psram.lf"
    )
    set(PAL_TARGET_AUDIO_SOURCES
        "../../unix/contract_noaudio.c"
    )
endif()

set(PAL_ENGINE_REQUIRED_COMPONENTS
    esp_app_format
    esp_driver_gpio
    esp_driver_i2c
    esp_driver_sdspi
    esp_driver_spi
    esp_lcd
    esp_partition
    esp_timer
    fatfs
    sdmmc
)
if(XIAOMIAO_WROVER)
    list(APPEND PAL_ENGINE_REQUIRED_COMPONENTS
        esp_driver_gptimer
        esp_driver_ledc
    )
endif()
set(PAL_ENGINE_HOST_SOURCES
    "../../battle.c"
    "../../ending.c"
    "../../fight.c"
    "../../game.c"
    "../../global.c"
    "../../input.c"
    "../../itemmenu.c"
    "../../magicmenu.c"
    "../../main.c"
    "../../map.c"
    "../../overlay.c"
    "../../palcfg.c"
    "../../palcommon.c"
    "../../paldebug.c"
    "../../palette.c"
    "../../play.c"
    "../../res.c"
    "../../rngplay.c"
    "../../scene.c"
    "../../script.c"
    "../../ui.c"
    "../../uibattle.c"
    "../../uigame.c"
    "../../util.c"
    "../../video.c"
    "../../sdl_compat/sdl_compat.c"
    "../../unix/embedded_contract_stubs.c"
    "../../embedded/pal_pack.c"
    "../../embedded/pal_memory_profile.c"
    "../../embedded/pal_text_cache.c"
    "../../embedded/pal_font_cache.c"
    "../engine_bridge/pal_engine_app_main.c"
    "../engine_bridge/pal_engine_fatfs_stdio.c"
    "../engine_bridge/pal_engine_pack_provider.c"
    "../engine_bridge/pal_engine_posix_shims.c"
    "../engine_bridge/pal_engine_target_input.c"
    "../engine_bridge/pal_engine_target_packs.c"
    "../engine_bridge/pal_engine_target_time.c"
    "../engine_bridge/pal_engine_target_video.c"
    "../native_engine_shim/sdl_shim.c"
    ${PAL_TARGET_AUDIO_SOURCES}
    ${PAL_TARGET_BOARD_SOURCES}
)

if(CARDPUTER_EXTREME_CHAPTER_CACHE)
    list(APPEND PAL_ENGINE_HOST_SOURCES
        "../engine_bridge/pal_engine_chapter_cache.c"
    )
endif()

if(CARDPUTER_EXTREME_NO_PSRAM)
    list(APPEND PAL_ENGINE_HOST_SOURCES
        "../../embedded/pal_event_pager.c"
        "../engine_bridge/pal_engine_event_state.c"
    )
endif()

set_source_files_properties("../../main.c" PROPERTIES COMPILE_DEFINITIONS "main=PAL_EngineMain")

idf_component_register(
    SRCS
        ${PAL_ENGINE_HOST_SOURCES}
    INCLUDE_DIRS
        "."
        "../engine_bridge"
        "../engine_bridge/include"
        "../native_engine_shim/include"
        "../.."
        "../../embedded"
        "../../sdl_compat"
    REQUIRES
        ${PAL_ENGINE_REQUIRED_COMPONENTS}
    ${PAL_ENGINE_LDFRAGMENT_ARGS}
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    PAL_HEADLESS_SDL_SHIM=1
    PAL_HAS_JOYSTICKS=0
    PAL_HAS_TOUCH=0
    PAL_HAS_MP3=0
    PAL_HAS_OGG=0
    PAL_HAS_OPUS=0
    PAL_HAS_NATIVEMIDI=0
    PAL_HAS_GLSL=0
    PAL_HAS_CONFIG_PAGE=0
    PAL_NO_LAUNCH_UI
    PAL_NO_RUNTIME_DECOMPRESS=1
    PAL_NO_RUNTIME_HEAP=1
    PAL_CONTRACT_EXTERNAL_RIX=1
    PAL_CONTRACT_TARGET_PACK_PROVIDER=1
    PAL_ENGINE_BRIDGE_REQUIRE_TARGET_HOOKS=1
    USE_RIX_EXTRA_INIT=0
    PAL_ESP_CORES3SE_NO_SFX=1
    USE_SDL3=0
)

if(XIAOMIAO_WROVER)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        MEM_LEVEL2=1
        PAL_TARGET_XIAOMIAO=1
        PAL_STORAGE_SD_ONLY=1
        PAL_EXTREME_TWO_SCREENS=1
        PAL_EXTREME_SPRITES_TO_DRAW=512
        PAL_GLOBAL_BUFFER_SIZE=256
        PAL_SDL_SHIM_EXTERNAL_SURFACES_ONLY=1
        PAL_ENGINE_TF_TOC_BYTES=40960u
        PAL_LOG_BUFFER_SIZE=1024
        PAL_EXTREME_RIX_MUSIC=1
        PAL_CONTRACT_NO_SFX=1
        PAL_TARGET_GURU_MEDITATION=1
        SHIM_MAX_SURFACES=6
        SHIM_MAX_PALETTES=1
        SHIM_MAX_TEXTURES=1
        SHIM_SURFACE_PIXELS=1u
        SHIM_TEXTURE_PIXELS=1u
        SHIM_KEYBOARD_KEYS=256
        SHIM_EVENT_QUEUE=8
    )
elseif(CARDPUTER_EXTREME_NO_PSRAM)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        MEM_LEVEL1=1
        PAL_TARGET_CARDPUTER_ADV=1
        PAL_EXTREME_TWO_SCREENS=1
        PAL_EXTREME_SPRITES_TO_DRAW=512
        PAL_GLOBAL_BUFFER_SIZE=256
        PAL_SDL_SHIM_EXTERNAL_SURFACES_ONLY=1
        PAL_ENGINE_TF_TOC_BYTES=2048u
        PAL_LOG_BUFFER_SIZE=1024
        SHIM_MAX_SURFACES=6
        SHIM_MAX_PALETTES=1
        SHIM_MAX_TEXTURES=1
        SHIM_SURFACE_PIXELS=1u
        SHIM_TEXTURE_PIXELS=1u
        SHIM_KEYBOARD_KEYS=256
        SHIM_EVENT_QUEUE=8
    )
    if(CARDPUTER_EXTREME_CHAPTER_CACHE)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            PAL_EXTREME_CHAPTER_CACHE=1
        )
    endif()
    if(CARDPUTER_EXTREME_MUSIC)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            PAL_EXTREME_RIX_MUSIC=1
            PAL_CONTRACT_NO_SFX=1
        )
    else()
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            PAL_CONTRACT_NO_AUDIO=1
            PAL_ESP_CORES3SE_NO_AUDIO=1
        )
    endif()
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        MEM_LEVEL2=1
        PAL_CONTRACT_NO_AUDIO=1
        PAL_ESP_CORES3SE_NO_AUDIO=1
        SHIM_MAX_SURFACES=8
        SHIM_MAX_PALETTES=8
        SHIM_MAX_TEXTURES=1
        SHIM_SURFACE_PIXELS=65536u
    )
endif()

target_compile_options(${COMPONENT_LIB} PRIVATE
    -fstack-usage
    -fno-strict-aliasing
    -Wno-error
    -Wno-error=implicit-fallthrough
    -Wno-error=unused-but-set-parameter
)

target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=PAL_MKFOpenPackArchive"
    "-Wl,--wrap=PAL_MKFIsPackArchive"
    "-Wl,--wrap=PAL_MKFMapChunk"
    "-Wl,--wrap=PAL_MKFGetChunkCount"
    "-Wl,--wrap=PAL_MKFGetChunkSize"
    "-Wl,--wrap=PAL_MKFReadChunk"
    "-Wl,--wrap=fopen"
    "-Wl,--wrap=access"
    "-Wl,--wrap=fclose"
    "-Wl,--wrap=fread"
    "-Wl,--wrap=fwrite"
    "-Wl,--wrap=fseek"
    "-Wl,--wrap=ftell"
    "-Wl,--wrap=fgets"
    "-Wl,--wrap=fputs"
    "-Wl,--wrap=fflush"
    "-Wl,--wrap=feof"
    "-Wl,--wrap=ferror"
    "-Wl,--wrap=rewind"
)
endif()
