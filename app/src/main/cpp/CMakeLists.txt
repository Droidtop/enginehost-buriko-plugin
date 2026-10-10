cmake_minimum_required(VERSION 3.22)
project(enginehost_openbgi C)

# OpenBGI runs in Enginehost's sandbox (BurikoPlugin.java, os_enginehost.c):
# no window, no audio device, no storage permission. SDL is still the
# engine's threads, atomics and surfaces, and is linked in statically: the
# isolated runtime loads this one library from a descriptor, and SDL's own
# libc calls then sit inside the wrapped library too. Its device subsystems
# are never initialised; HIDAPI is left out because nothing here opens a pad
# through SDL and it is the only C++ in SDL's Android build.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/SDL EXCLUDE_FROM_ALL)

set(ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../src)
file(GLOB OPENBGI_SOURCES CONFIGURE_DEPENDS ${ENGINE_DIR}/*.c)
# os.c is the desktop's SDL window, input and sound; os_enginehost.c below
# is this build's.
list(REMOVE_ITEM OPENBGI_SOURCES ${ENGINE_DIR}/os.c)
if(NOT OPENBGI_SOURCES)
    message(FATAL_ERROR "No engine sources at ${ENGINE_DIR}; build a release branch, which carries the engine.")
endif()

set(PLUGIN_NATIVE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../plugin-native)

# logcat.c moves stdout and stderr onto __android_log_print, which is where
# every printf in src/ then arrives.
add_library(main SHARED
    ${OPENBGI_SOURCES}
    logcat.c
    os_enginehost.c
    ${PLUGIN_NATIVE_DIR}/enginehost_audio_ring.c)
target_include_directories(main PRIVATE ${ENGINE_DIR} ${PLUGIN_NATIVE_DIR})
target_compile_definitions(main PRIVATE SPNG_USE_MINIZ main=SDL_main)
target_compile_options(main PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(main PRIVATE SDL2::SDL2-static android log m)

# Every file the engine opens goes through Enginehost's file layer.
include(${PLUGIN_NATIVE_DIR}/enginehost_vfs.cmake)
enginehost_vfs(main)
