cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(atlas_rover_dualeye)

spiffs_create_partition_image(
    storage
    ../../assets/dualeye_sdcard_v0_1/sdcard
    FLASH_IN_PROJECT
)
