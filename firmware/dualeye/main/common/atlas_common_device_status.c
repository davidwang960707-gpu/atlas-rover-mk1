#include "common/atlas_common_device_status.h"

#include <stdio.h>
#include <string.h>

const char *atlas_common_check_status_name(atlas_common_check_status_t status)
{
    switch (status) {
    case ATLAS_COMMON_CHECK_PASS:
        return "pass";
    case ATLAS_COMMON_CHECK_WARN:
        return "warn";
    case ATLAS_COMMON_CHECK_FAIL:
    default:
        return "fail";
    }
}

atlas_common_check_status_t atlas_common_check_status_from_name(const char *status)
{
    if (status == NULL) {
        return ATLAS_COMMON_CHECK_FAIL;
    }
    if (strcmp(status, "pass") == 0) {
        return ATLAS_COMMON_CHECK_PASS;
    }
    if (strcmp(status, "warn") == 0) {
        return ATLAS_COMMON_CHECK_WARN;
    }
    return ATLAS_COMMON_CHECK_FAIL;
}

atlas_common_check_status_t atlas_common_check_status_from_flags(bool pass, bool warn)
{
    if (pass) {
        return ATLAS_COMMON_CHECK_PASS;
    }
    if (warn) {
        return ATLAS_COMMON_CHECK_WARN;
    }
    return ATLAS_COMMON_CHECK_FAIL;
}

void atlas_common_selftest_summary_reset(atlas_common_selftest_summary_t *summary)
{
    if (summary == NULL) {
        return;
    }
    summary->pass = 0;
    summary->warn = 0;
    summary->fail = 0;
}

void atlas_common_selftest_summary_count(atlas_common_selftest_summary_t *summary,
                                         atlas_common_check_status_t status)
{
    if (summary == NULL) {
        return;
    }
    switch (status) {
    case ATLAS_COMMON_CHECK_PASS:
        ++summary->pass;
        break;
    case ATLAS_COMMON_CHECK_WARN:
        ++summary->warn;
        break;
    case ATLAS_COMMON_CHECK_FAIL:
    default:
        ++summary->fail;
        break;
    }
}

void atlas_common_selftest_summary_count_name(atlas_common_selftest_summary_t *summary,
                                              const char *status)
{
    atlas_common_selftest_summary_count(summary, atlas_common_check_status_from_name(status));
}

bool atlas_common_selftest_ready(const atlas_common_selftest_summary_t *summary)
{
    return summary != NULL && summary->fail == 0;
}

size_t atlas_common_selftest_summary_write_json(const atlas_common_selftest_summary_t *summary,
                                                char *dst,
                                                size_t dst_size)
{
    if (dst == NULL || dst_size == 0) {
        return 0;
    }
    if (summary == NULL) {
        return (size_t)snprintf(dst, dst_size, "{\"pass\":0,\"warn\":0,\"fail\":1}");
    }
    return (size_t)snprintf(dst,
                            dst_size,
                            "{\"pass\":%u,\"warn\":%u,\"fail\":%u}",
                            summary->pass,
                            summary->warn,
                            summary->fail);
}

const char *atlas_common_device_status_protocol(void)
{
    return ATLAS_COMMON_DEVICE_STATUS_PROTOCOL;
}

const char *atlas_common_selftest_protocol(void)
{
    return ATLAS_COMMON_SELFTEST_PROTOCOL;
}

const char *atlas_common_capabilities_protocol(void)
{
    return ATLAS_COMMON_CAPABILITIES_PROTOCOL;
}
