cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED PAL_CORES3SE_ENGINE_HOST)
    set(PAL_CORES3SE_ENGINE_HOST ON CACHE BOOL "Build the CoreS3 SE full-engine host")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sdlpal_cores3se)
