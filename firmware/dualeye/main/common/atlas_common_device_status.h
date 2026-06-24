#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ATLAS_COMMON_DEVICE_STATUS_PROTOCOL "atlas.device.status.v0"
#define ATLAS_COMMON_SELFTEST_PROTOCOL "atlas.selftest.v0"
#define ATLAS_COMMON_CAPABILITIES_PROTOCOL "atlas.capabilities.v0"

typedef enum {
    ATLAS_COMMON_CHECK_PASS = 0,
    ATLAS_COMMON_CHECK_WARN,
    ATLAS_COMMON_CHECK_FAIL,
} atlas_common_check_status_t;

typedef struct {
    uint8_t pass;
    uint8_t warn;
    uint8_t fail;
} atlas_common_selftest_summary_t;

const char *atlas_common_check_status_name(atlas_common_check_status_t status);
atlas_common_check_status_t atlas_common_check_status_from_name(const char *status);
atlas_common_check_status_t atlas_common_check_status_from_flags(bool pass, bool warn);

void atlas_common_selftest_summary_reset(atlas_common_selftest_summary_t *summary);
void atlas_common_selftest_summary_count(atlas_common_selftest_summary_t *summary,
                                         atlas_common_check_status_t status);
void atlas_common_selftest_summary_count_name(atlas_common_selftest_summary_t *summary,
                                              const char *status);
bool atlas_common_selftest_ready(const atlas_common_selftest_summary_t *summary);
size_t atlas_common_selftest_summary_write_json(const atlas_common_selftest_summary_t *summary,
                                                char *dst,
                                                size_t dst_size);

const char *atlas_common_device_status_protocol(void);
const char *atlas_common_selftest_protocol(void);
const char *atlas_common_capabilities_protocol(void);
