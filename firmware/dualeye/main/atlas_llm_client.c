#include "atlas_llm_client.h"

#include <string.h>

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    strlcpy(dst, src, dst_size);
}

bool atlas_llm_client_ready(const atlas_config_t *config)
{
    if (config == NULL) {
        return false;
    }
    if (strcmp(config->llm.mode, "off") == 0 || config->llm.mode[0] == '\0') {
        return false;
    }
    if (strcmp(config->llm.mode, "host") == 0) {
        return config->llm.base_url[0] != '\0';
    }
    return config->llm.base_url[0] != '\0' &&
           config->llm.model[0] != '\0' &&
           atlas_config_has_llm_api_key(config);
}

const char *atlas_llm_client_mode_label(const char *mode)
{
    if (mode == NULL || mode[0] == '\0' || strcmp(mode, "off") == 0) {
        return "关闭";
    }
    if (strcmp(mode, "host") == 0) {
        return "外部宿主/调试桥";
    }
    if (strcmp(mode, "embedded") == 0) {
        return "端侧 Atlas Brain";
    }
    if (strcmp(mode, "cloud") == 0) {
        return "云端大模型";
    }
    return "未知模式";
}

void atlas_llm_client_get_status(const atlas_config_t *config, atlas_llm_status_t *status)
{
    if (status == NULL) {
        return;
    }

    memset(status, 0, sizeof(*status));
    if (config == NULL) {
        copy_string(status->mode, sizeof(status->mode), "off");
        return;
    }

    status->configured = atlas_llm_client_ready(config);
    status->api_key_set = atlas_config_has_llm_api_key(config);
    copy_string(status->mode, sizeof(status->mode), config->llm.mode);
    copy_string(status->provider, sizeof(status->provider), config->llm.provider);
    copy_string(status->base_url, sizeof(status->base_url), config->llm.base_url);
    copy_string(status->model, sizeof(status->model), config->llm.model);
}
