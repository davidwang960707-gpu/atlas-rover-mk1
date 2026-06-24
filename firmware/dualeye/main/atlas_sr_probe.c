#include "atlas_sr_probe.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "sdkconfig.h"

#ifndef __has_include
#define __has_include(x) 0
#endif

#if __has_include("esp_afe_sr_iface.h")
#define ATLAS_ESP_SR_HEADER_PRESENT 1
#include "esp_wn_models.h"
#include "model_path.h"
#else
#define ATLAS_ESP_SR_HEADER_PRESENT 0
#endif

#if __has_include("esp_opus_enc.h")
#define ATLAS_OPUS_HEADER_PRESENT 1
#else
#define ATLAS_OPUS_HEADER_PRESENT 0
#endif

static const esp_partition_t *model_partition(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                    ESP_PARTITION_SUBTYPE_ANY,
                                    "model");
}

static bool copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0 || src == NULL) {
        return false;
    }
    snprintf(dst, dst_size, "%s", src);
    return true;
}

size_t atlas_sr_probe_write_json(char *dst, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) {
        return 0;
    }

    const bool header_present = ATLAS_ESP_SR_HEADER_PRESENT != 0;
    const bool opus_present = ATLAS_OPUS_HEADER_PRESENT != 0;
    const bool build_enabled = header_present;
    const esp_partition_t *partition = model_partition();
    const bool model_found = partition != NULL;
    const uint32_t model_offset = partition ? partition->address : 0;
    const uint32_t model_size = partition ? partition->size : 0;
    const uint32_t free_heap = esp_get_free_heap_size();
    const uint32_t min_free_heap = esp_get_minimum_free_heap_size();
    const uint32_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const uint32_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t largest_spiram = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    int model_count = -1;
    int wakenet_sample_rate = 0;
    int wakenet_chunk_samples = 0;
    int wakenet_word_count = 0;
    bool model_load_ok = false;
    bool wakenet_model_found = false;
    bool wakenet_create_ok = false;
    char first_model[64] = "";
    char wakenet_model[64] = "";
    char wake_words[160] = "";

#if ATLAS_ESP_SR_HEADER_PRESENT
    if (model_found) {
        srmodel_list_t *models = esp_srmodel_init("model");
        if (models != NULL) {
            model_count = models->num;
            model_load_ok = model_count >= 0;
            if (model_count > 0 && models->model_name != NULL && models->model_name[0] != NULL) {
                copy_text(first_model, sizeof(first_model), models->model_name[0]);
            }
            char *wn_name = esp_srmodel_filter(models, ESP_WN_PREFIX, NULL);
            if (wn_name != NULL) {
                wakenet_model_found = true;
                copy_text(wakenet_model, sizeof(wakenet_model), wn_name);
                char *words = esp_srmodel_get_wake_words(models, wn_name);
                copy_text(wake_words, sizeof(wake_words), words);
                const esp_wn_iface_t *iface = esp_wn_handle_from_name(wn_name);
                if (iface != NULL) {
                    model_iface_data_t *wn = iface->create(wn_name, DET_MODE_95);
                    if (wn != NULL) {
                        wakenet_create_ok = true;
                        wakenet_sample_rate = iface->get_samp_rate(wn);
                        wakenet_chunk_samples = iface->get_samp_chunksize(wn);
                        wakenet_word_count = iface->get_word_num(wn);
                        iface->destroy(wn);
                    }
                }
            }
            esp_srmodel_deinit(models);
        }
    }
#endif

    const bool ready_for_wakenet = header_present && build_enabled && model_found && model_load_ok &&
                                   wakenet_model_found && wakenet_create_ok;

    return snprintf(dst,
                    dst_size,
                    "{\"stage\":\"P3_resource_probe\",\"build_enabled\":%s,"
                    "\"esp_sr_header_present\":%s,\"opus_header_present\":%s,"
                    "\"afe_enabled\":false,\"wakenet_enabled\":false,\"vadnet_enabled\":false,\"aec_enabled\":false,"
                    "\"current_wake_engine\":\"energy_gate_vad\",\"fallback_active\":true,"
                    "\"configured_wakenet\":\"%s\","
                    "\"model_partition_found\":%s,\"model_partition\":{\"offset\":\"0x%lx\",\"size\":%lu},"
                    "\"model_load_ok\":%s,\"model_count\":%d,\"first_model\":\"%s\","
                    "\"wakenet_model_found\":%s,\"wakenet_create_ok\":%s,\"wakenet_model\":\"%s\","
                    "\"wake_words\":\"%s\",\"wakenet_sample_rate\":%d,\"wakenet_chunk_samples\":%d,"
                    "\"wakenet_word_count\":%d,\"ready_for_wakenet\":%s,"
                    "\"memory\":{\"free_heap\":%u,\"min_free_heap\":%u,\"free_spiram\":%u,"
                    "\"largest_internal\":%u,\"largest_spiram\":%u},"
                    "\"risk\":\"%s\","
                    "\"aec_plan\":\"playback_mute_now__afe_aec_requires_reference_input_validation\","
                    "\"test_matrix\":[\"energy_gate_baseline\",\"opus_probe_plus_vad\",\"wakenet_model_init\",\"wakenet_task_ab\",\"aec_reference_check\",\"wakenet_plus_aec\"],"
                    "\"next_steps\":[\"flash srmodels/srmodels.bin\",\"verify this endpoint on device\",\"run /api/audio/opus-probe\",\"enable wakenet task only after heap/cpu pass\",\"verify AEC reference path before enabling AFE AEC\"]}",
                    build_enabled ? "true" : "false",
                    header_present ? "true" : "false",
                    opus_present ? "true" : "false",
#if CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS
                    "wn9_nihaoxiaozhi_tts",
#else
                    "none",
#endif
                    model_found ? "true" : "false",
                    (unsigned long)model_offset,
                    (unsigned long)model_size,
                    model_load_ok ? "true" : "false",
                    model_count,
                    first_model,
                    wakenet_model_found ? "true" : "false",
                    wakenet_create_ok ? "true" : "false",
                    wakenet_model,
                    wake_words,
                    wakenet_sample_rate,
                    wakenet_chunk_samples,
                    wakenet_word_count,
                    ready_for_wakenet ? "true" : "false",
                    (unsigned)free_heap,
                    (unsigned)min_free_heap,
                    (unsigned)free_spiram,
                    (unsigned)largest_internal,
                    (unsigned)largest_spiram,
                    ready_for_wakenet ? "ready_for_wakenet_task_ab" : "blocked_missing_esp_sr_or_model_or_init");
}
