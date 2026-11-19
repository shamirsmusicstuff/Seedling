# Wraps the finished Seedling.clap (built by nih-plug) into an Audio Unit (AUv2) with clap-wrapper (MIT).
# Usage (macOS):  cmake -S rust/au -B build-au -DSEEDLING_CLAP=<path to Seedling.clap> -DSEEDLING_VERSION=0.6.0
#                 cmake --build build-au --config Release      -> build-au/Seedling.component
cmake_minimum_required(VERSION 3.21)
project(SeedlingAU VERSION 0.6.0 LANGUAGES C CXX OBJC OBJCXX)

if(NOT APPLE)
    message(FATAL_ERROR "The Audio Unit wrapper only builds on macOS")
endif()
if(NOT SEEDLING_CLAP OR NOT EXISTS "${SEEDLING_CLAP}")
    message(FATAL_ERROR "Pass -DSEEDLING_CLAP=<path to the built Seedling.clap bundle>")
endif()
if(NOT SEEDLING_VERSION)
    set(SEEDLING_VERSION "0.6.0")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")

set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES TRUE CACHE BOOL "")
set(CLAP_WRAPPER_BUILD_AUV2 TRUE CACHE BOOL "")
set(CLAP_WRAPPER_COPY_AFTER_BUILD FALSE CACHE BOOL "")

include(FetchContent)
FetchContent_Declare(clap_wrapper
    GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git
    GIT_TAG 806bba0a5f792e3966b99c1561ab9952d3316d7c)
FetchContent_MakeAvailable(clap_wrapper)

add_library(seedling_auv2 MODULE)
target_add_auv2_wrapper(
    TARGET seedling_auv2
    OUTPUT_NAME "Seedling"
    BUNDLE_IDENTIFIER "com.shamirsmusicstuff.seedling.auv2"
    BUNDLE_VERSION "${SEEDLING_VERSION}"
    MANUFACTURER_NAME "Seedling Audio"
    MANUFACTURER_CODE "Sdlg"
    SUBTYPE_CODE "Sdrs"
    INSTRUMENT_TYPE "aumu"
    MACOS_EMBEDDED_CLAP_LOCATION "${SEEDLING_CLAP}")
