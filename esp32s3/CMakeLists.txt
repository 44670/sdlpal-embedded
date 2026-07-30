cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED PAL_CORES3SE_ENGINE_HOST)
    set(PAL_CORES3SE_ENGINE_HOST ON CACHE BOOL "Build the CoreS3 SE full-engine host")
endif()

if(NOT DEFINED CARDPUTER_EXTREME_NO_PSRAM)
    set(CARDPUTER_EXTREME_NO_PSRAM OFF CACHE BOOL
        "Build the Cardputer ADV 8MB/no-PSRAM/two-screen extreme profile")
endif()

if(CARDPUTER_EXTREME_NO_PSRAM AND NOT PAL_CORES3SE_ENGINE_HOST)
    message(FATAL_ERROR "CARDPUTER_EXTREME_NO_PSRAM requires PAL_CORES3SE_ENGINE_HOST=ON")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(CARDPUTER_EXTREME_NO_PSRAM)
    project(sdlpal_cardputer_extreme)
else()
    project(sdlpal_cores3se)
endif()
