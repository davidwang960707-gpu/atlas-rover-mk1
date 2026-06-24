#pragma once

#include <stddef.h>

#ifndef ATLAS_ESP_SR_BUILD_ENABLED
#define ATLAS_ESP_SR_BUILD_ENABLED 0
#endif

size_t atlas_sr_probe_write_json(char *dst, size_t dst_size);
