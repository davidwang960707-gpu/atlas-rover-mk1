#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#define ATLAS_COMMON_TOOL_SCHEMA_VERSION "atlas.tools.v0.desk_apps"
#define ATLAS_COMMON_TOOL_SCHEMA_PROTOCOL "atlas.tools.v0"

typedef enum {
    ATLAS_COMMON_TOOL_RESULT_OK = 0,
    ATLAS_COMMON_TOOL_RESULT_INVALID_ARGUMENT,
    ATLAS_COMMON_TOOL_RESULT_NOT_FOUND,
    ATLAS_COMMON_TOOL_RESULT_NOT_SUPPORTED,
    ATLAS_COMMON_TOOL_RESULT_INVALID_STATE,
    ATLAS_COMMON_TOOL_RESULT_REQUIRES_CONFIRMATION,
    ATLAS_COMMON_TOOL_RESULT_FAILED,
} atlas_common_tool_result_t;

typedef struct {
    const char *name;
    const char *risk;
    const char *target;
    bool enabled;
} atlas_common_tool_descriptor_t;

const char *atlas_common_tool_schema_version(void);
const char *atlas_common_tool_schema_protocol(void);
const atlas_common_tool_descriptor_t *atlas_common_tool_schema_descriptors(size_t *count);
size_t atlas_common_tool_schema_tool_count(void);
bool atlas_common_tool_schema_has_tool(const char *name);

atlas_common_tool_result_t atlas_common_tool_result_from_esp_err(esp_err_t err);
const char *atlas_common_tool_result_code(atlas_common_tool_result_t result);
const char *atlas_common_tool_result_code_from_esp_err(esp_err_t err);

size_t atlas_common_tool_schema_write_list_json(char *dst, size_t dst_size);
size_t atlas_common_tool_schema_write_call_result_json(char *dst,
                                                       size_t dst_size,
                                                       const char *tool,
                                                       const char *result,
                                                       const char *page,
                                                       const char *expression,
                                                       esp_err_t err);
