cmake_minimum_required(VERSION 3.22)
project(enginehost_openbgi C)

add_subdirectory(vendor/SDL EXCLUDE_FROM_ALL)
file(GLOB OPENBGI_SOURCES CONFIGURE_DEPENDS ../../../../src/*.c)

add_library(main SHARED ${OPENBGI_SOURCES})
target_include_directories(main PRIVATE ../../../../src)
target_compile_definitions(main PRIVATE SPNG_USE_MINIZ main=SDL_main)
target_compile_options(main PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(main PRIVATE SDL2::SDL2 SDL2::SDL2main android log m)
