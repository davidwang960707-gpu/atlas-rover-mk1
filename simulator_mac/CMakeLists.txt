cmake_minimum_required(VERSION 3.16)
project(atlas_dualeye_sim C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_program(SDL2_CONFIG sdl2-config REQUIRED)
execute_process(COMMAND ${SDL2_CONFIG} --cflags OUTPUT_VARIABLE SDL2_CFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${SDL2_CONFIG} --libs OUTPUT_VARIABLE SDL2_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE)

separate_arguments(SDL2_CFLAGS_LIST UNIX_COMMAND "${SDL2_CFLAGS}")
separate_arguments(SDL2_LIBS_LIST UNIX_COMMAND "${SDL2_LIBS}")

add_executable(atlas_dualeye_sim src/main.c)
target_compile_options(atlas_dualeye_sim PRIVATE ${SDL2_CFLAGS_LIST} -Wall -Wextra -Wpedantic)
target_link_libraries(atlas_dualeye_sim PRIVATE ${SDL2_LIBS_LIST})
