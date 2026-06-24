#include "common/atlas_common_config.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

const char *atlas_common_config_protocol(void)
{
    return ATLAS_COMMON_CONFIG_PROTOCOL;
}

const char *atlas_common_wifi_provisioning_protocol(void)
{
    return ATLAS_COMMON_WIFI_PROVISIONING_PROTOCOL;
}

const char *atlas_common_config_default_device_name(void)
{
    return ATLAS_COMMON_CONFIG_DEFAULT_DEVICE_NAME;
}

const char *atlas_common_config_default_llm_mode(void)
{
    return ATLAS_COMMON_CONFIG_DEFAULT_LLM_MODE;
}

const char *atlas_common_config_default_llm_provider(void)
{
    return ATLAS_COMMON_CONFIG_DEFAULT_LLM_PROVIDER;
}

void atlas_common_config_copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_size, "%s", src);
}

void atlas_common_config_trim(char *text)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }

    char *start = text;
    while (isspace((unsigned char)*start)) {
        ++start;
    }
    if (*start == '\0') {
        text[0] = '\0';
        return;
    }

    char *end = start + strlen(start);
    while (end > start && isspace((unsigned char)*(end - 1))) {
        --end;
    }
    *end = '\0';

    if (start != text) {
        memmove(text, start, (size_t)(end - start + 1));
    }
}

bool atlas_common_config_has_value(const char *text)
{
    if (text == NULL) {
        return false;
    }
    while (*text != '\0') {
        if (!isspace((unsigned char)*text)) {
            return true;
        }
        ++text;
    }
    return false;
}

bool atlas_common_config_llm_mode_is_known(const char *mode)
{
    return mode != NULL &&
           (strcmp(mode, "off") == 0 ||
            strcmp(mode, "host") == 0 ||
            strcmp(mode, "cloud") == 0 ||
            strcmp(mode, "embedded") == 0);
}

bool atlas_common_config_llm_provider_is_known(const char *provider)
{
    return provider != NULL &&
           (strcmp(provider, "openai_compatible") == 0 ||
            strcmp(provider, "deepseek") == 0 ||
            strcmp(provider, "host") == 0 ||
            strcmp(provider, "mimo") == 0);
}
