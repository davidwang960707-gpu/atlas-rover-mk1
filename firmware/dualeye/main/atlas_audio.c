#include "atlas_audio.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"

static const char *TAG = "atlas_audio";

/*
 * Waveshare ESP32-S3-DualEye-Touch-LCD-1.28 official audio pin map.
 * ES8311 drives speaker output through PA GPIO9; ES7210 captures onboard mics.
 */
#define ATLAS_AUDIO_I2C_PORT I2C_NUM_0
#define ATLAS_AUDIO_I2C_SDA GPIO_NUM_11
#define ATLAS_AUDIO_I2C_SCL GPIO_NUM_10
#define ATLAS_AUDIO_I2C_FREQ_HZ 400000

#define ATLAS_AUDIO_I2S_PORT I2S_NUM_0
#define ATLAS_AUDIO_I2S_MCLK GPIO_NUM_12
#define ATLAS_AUDIO_I2S_BCLK GPIO_NUM_13
#define ATLAS_AUDIO_I2S_WS GPIO_NUM_14
#define ATLAS_AUDIO_I2S_DIN GPIO_NUM_15
#define ATLAS_AUDIO_I2S_DOUT GPIO_NUM_16
#define ATLAS_AUDIO_PA_PIN GPIO_NUM_9

#define ATLAS_AUDIO_CHANNELS 2
#define ATLAS_AUDIO_BITS 16
#define ATLAS_AUDIO_BEEP_CHUNK_FRAMES 160
#define ATLAS_AUDIO_MIC_CHUNK_FRAMES 160
#define ATLAS_AUDIO_WAV_HEADER_BYTES 44
#define ATLAS_AUDIO_WAV_PLAY_CHUNK_FRAMES 160
#define ATLAS_AUDIO_WAV_FADE_FRAMES 160u
#define ATLAS_AUDIO_WAV_TAIL_SILENCE_FRAMES 1600u

typedef struct {
    i2s_chan_handle_t tx;
    i2s_chan_handle_t rx;
} atlas_i2s_pair_t;

static SemaphoreHandle_t s_audio_lock;
static i2c_master_bus_handle_t s_i2c_bus;
static atlas_i2s_pair_t s_i2s;
static const audio_codec_data_if_t *s_data_if;
static esp_codec_dev_handle_t s_output_dev;
static esp_codec_dev_handle_t s_input_dev;
static atlas_audio_status_t s_status = {
    .sample_rate = ATLAS_AUDIO_SAMPLE_RATE,
    .volume = 60,
    .last_error = ESP_ERR_INVALID_STATE,
};

static const int16_t s_sine_64[64] = {
    0, 3212, 6393, 9512, 12539, 15446, 18204, 20787,
    23170, 25330, 27245, 28897, 30273, 31356, 32138, 32610,
    32767, 32610, 32138, 31356, 30273, 28897, 27245, 25330,
    23170, 20787, 18204, 15446, 12539, 9512, 6393, 3212,
    0, -3212, -6393, -9512, -12539, -15446, -18204, -20787,
    -23170, -25330, -27245, -28897, -30273, -31356, -32138, -32610,
    -32767, -32610, -32138, -31356, -30273, -28897, -27245, -25330,
    -23170, -20787, -18204, -15446, -12539, -9512, -6393, -3212,
};

static void remember_error(esp_err_t err)
{
    if (err != ESP_OK) {
        s_status.last_error = err;
    }
}

static uint32_t isqrt_u64(uint64_t value)
{
    uint64_t bit = 1ull << 62;
    while (bit > value) {
        bit >>= 2;
    }

    uint64_t result = 0;
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)result;
}

static void write_le16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xffu);
    dst[1] = (uint8_t)((value >> 8) & 0xffu);
}

static void write_le32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xffu);
    dst[1] = (uint8_t)((value >> 8) & 0xffu);
    dst[2] = (uint8_t)((value >> 16) & 0xffu);
    dst[3] = (uint8_t)((value >> 24) & 0xffu);
}

static uint16_t read_le16(const uint8_t *src)
{
    return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static uint32_t read_le32(const uint8_t *src)
{
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

static void write_wav_header(uint8_t *dst, uint32_t sample_rate, uint16_t channels, uint16_t bits, uint32_t data_bytes)
{
    memcpy(dst, "RIFF", 4);
    write_le32(dst + 4, 36u + data_bytes);
    memcpy(dst + 8, "WAVE", 4);
    memcpy(dst + 12, "fmt ", 4);
    write_le32(dst + 16, 16);
    write_le16(dst + 20, 1);
    write_le16(dst + 22, channels);
    write_le32(dst + 24, sample_rate);
    write_le32(dst + 28, sample_rate * channels * (bits / 8u));
    write_le16(dst + 32, channels * (bits / 8u));
    write_le16(dst + 34, bits);
    memcpy(dst + 36, "data", 4);
    write_le32(dst + 40, data_bytes);
}

static void *audio_malloc(size_t size)
{
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == NULL) {
        ptr = malloc(size);
    }
    return ptr;
}

static esp_err_t ensure_lock(void)
{
    if (s_audio_lock == NULL) {
        s_audio_lock = xSemaphoreCreateMutex();
    }
    return s_audio_lock == NULL ? ESP_ERR_NO_MEM : ESP_OK;
}

static esp_err_t audio_i2c_init(void)
{
    if (s_i2c_bus != NULL) {
        s_status.i2c_ready = true;
        return ESP_OK;
    }

    const i2c_master_bus_config_t bus_config = {
        .i2c_port = ATLAS_AUDIO_I2C_PORT,
        .sda_io_num = ATLAS_AUDIO_I2C_SDA,
        .scl_io_num = ATLAS_AUDIO_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
    };

    esp_err_t err = i2c_new_master_bus(&bus_config, &s_i2c_bus);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "audio I2C bus already initialized by another module");
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        s_status.i2c_ready = true;
        ESP_LOGI(TAG, "audio I2C ready: SDA=%d SCL=%d", ATLAS_AUDIO_I2C_SDA, ATLAS_AUDIO_I2C_SCL);
    } else {
        remember_error(err);
    }
    return err;
}

static esp_err_t audio_i2s_init(void)
{
    if (s_i2s.tx != NULL && s_i2s.rx != NULL) {
        s_status.i2s_ready = true;
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(ATLAS_AUDIO_I2S_PORT, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&chan_cfg, &s_i2s.tx, &s_i2s.rx);
    if (err != ESP_OK) {
        remember_error(err);
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(ATLAS_AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = ATLAS_AUDIO_I2S_MCLK,
            .bclk = ATLAS_AUDIO_I2S_BCLK,
            .ws = ATLAS_AUDIO_I2S_WS,
            .dout = ATLAS_AUDIO_I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    err = i2s_channel_init_std_mode(s_i2s.tx, &std_cfg);
    if (err != ESP_OK) {
        remember_error(err);
        return err;
    }

    i2s_tdm_slot_mask_t slot_mask = I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3;
    i2s_tdm_config_t tdm_cfg = {
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, slot_mask),
        .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(ATLAS_AUDIO_SAMPLE_RATE),
        .gpio_cfg = {
            .mclk = ATLAS_AUDIO_I2S_MCLK,
            .bclk = ATLAS_AUDIO_I2S_BCLK,
            .ws = ATLAS_AUDIO_I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = ATLAS_AUDIO_I2S_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    tdm_cfg.slot_cfg.total_slot = 4;
    err = i2s_channel_init_tdm_mode(s_i2s.rx, &tdm_cfg);
    if (err != ESP_OK) {
        remember_error(err);
        return err;
    }

    err = i2s_channel_enable(s_i2s.tx);
    if (err == ESP_OK) {
        err = i2s_channel_enable(s_i2s.rx);
    }
    if (err == ESP_OK) {
        s_status.i2s_ready = true;
        ESP_LOGI(TAG,
                 "audio I2S ready: MCLK=%d BCLK=%d WS=%d DIN=%d DOUT=%d",
                 ATLAS_AUDIO_I2S_MCLK,
                 ATLAS_AUDIO_I2S_BCLK,
                 ATLAS_AUDIO_I2S_WS,
                 ATLAS_AUDIO_I2S_DIN,
                 ATLAS_AUDIO_I2S_DOUT);
    } else {
        remember_error(err);
    }
    return err;
}

static esp_err_t audio_data_if_init(void)
{
    if (s_data_if != NULL) {
        return ESP_OK;
    }

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = ATLAS_AUDIO_I2S_PORT,
        .rx_handle = s_i2s.rx,
        .tx_handle = s_i2s.tx,
    };
    s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (s_data_if == NULL) {
        remember_error(ESP_FAIL);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t output_codec_init(uint8_t volume)
{
    if (s_output_dev != NULL) {
        s_status.output_ready = true;
        return atlas_audio_set_volume(volume);
    }

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = ATLAS_AUDIO_I2C_PORT,
        .bus_handle = s_i2c_bus,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    if (ctrl_if == NULL || gpio_if == NULL) {
        remember_error(ESP_FAIL);
        return ESP_FAIL;
    }

    es8311_codec_cfg_t es8311_cfg = {
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .pa_pin = ATLAS_AUDIO_PA_PIN,
        .use_mclk = true,
    };
    es8311_cfg.hw_gain.pa_voltage = 5.0;
    es8311_cfg.hw_gain.codec_dac_voltage = 3.3;

    const audio_codec_if_t *codec_if = es8311_codec_new(&es8311_cfg);
    if (codec_if == NULL) {
        remember_error(ESP_FAIL);
        return ESP_FAIL;
    }

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = codec_if,
        .data_if = s_data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    s_output_dev = esp_codec_dev_new(&dev_cfg);
    if (s_output_dev == NULL) {
        remember_error(ESP_FAIL);
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = ATLAS_AUDIO_SAMPLE_RATE,
        .channel = ATLAS_AUDIO_CHANNELS,
        .bits_per_sample = ATLAS_AUDIO_BITS,
    };
    esp_err_t err = esp_codec_dev_open(s_output_dev, &fs);
    if (err == ESP_OK) {
        err = esp_codec_dev_set_out_vol(s_output_dev, volume);
    }
    if (err == ESP_OK) {
        (void)esp_codec_dev_set_out_mute(s_output_dev, false);
        s_status.output_ready = true;
        s_status.volume = volume;
        ESP_LOGI(TAG, "ES8311 speaker path ready, PA GPIO=%d volume=%u", ATLAS_AUDIO_PA_PIN, volume);
    } else {
        remember_error(err);
    }
    return err;
}

static esp_err_t input_codec_init(void)
{
    if (s_input_dev != NULL) {
        s_status.input_ready = true;
        return ESP_OK;
    }

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = ATLAS_AUDIO_I2C_PORT,
        .bus_handle = s_i2c_bus,
        .addr = ES7210_CODEC_DEFAULT_ADDR,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (ctrl_if == NULL) {
        remember_error(ESP_FAIL);
        return ESP_FAIL;
    }

    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = ctrl_if,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2,
    };
    const audio_codec_if_t *codec_if = es7210_codec_new(&es7210_cfg);
    if (codec_if == NULL) {
        remember_error(ESP_FAIL);
        return ESP_FAIL;
    }

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = codec_if,
        .data_if = s_data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    s_input_dev = esp_codec_dev_new(&dev_cfg);
    if (s_input_dev == NULL) {
        remember_error(ESP_FAIL);
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = ATLAS_AUDIO_SAMPLE_RATE,
        .channel = ATLAS_AUDIO_CHANNELS,
        .bits_per_sample = ATLAS_AUDIO_BITS,
    };
    esp_err_t err = esp_codec_dev_open(s_input_dev, &fs);
    if (err == ESP_OK) {
        err = esp_codec_dev_set_in_gain(s_input_dev, 30.0);
    }
    if (err == ESP_OK) {
        s_status.input_ready = true;
        ESP_LOGI(TAG, "ES7210 onboard microphone path ready");
    } else {
        remember_error(err);
    }
    return err;
}

esp_err_t atlas_audio_init(uint8_t volume)
{
    if (volume > 100) {
        volume = 100;
    }

    ESP_RETURN_ON_ERROR(ensure_lock(), TAG, "audio mutex failed");
    if (xSemaphoreTake(s_audio_lock, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = ESP_OK;
    esp_err_t first_err = ESP_OK;
    s_status.sample_rate = ATLAS_AUDIO_SAMPLE_RATE;
    s_status.volume = volume;

    err = audio_i2c_init();
    if (err != ESP_OK && first_err == ESP_OK) {
        first_err = err;
    }
    err = audio_i2s_init();
    if (err != ESP_OK && first_err == ESP_OK) {
        first_err = err;
    }
    if (first_err == ESP_OK) {
        err = audio_data_if_init();
        if (err != ESP_OK && first_err == ESP_OK) {
            first_err = err;
        }
    }
    if (first_err == ESP_OK) {
        err = output_codec_init(volume);
        if (err != ESP_OK && first_err == ESP_OK) {
            first_err = err;
        }
        err = input_codec_init();
        if (err != ESP_OK && first_err == ESP_OK) {
            first_err = err;
        }
    }

    s_status.initialized = s_status.input_ready || s_status.output_ready;
    if (s_status.initialized) {
        s_status.last_error = ESP_OK;
    }
    xSemaphoreGive(s_audio_lock);

    return s_status.initialized ? ESP_OK : (first_err == ESP_OK ? ESP_FAIL : first_err);
}

esp_err_t atlas_audio_set_volume(uint8_t volume)
{
    if (volume > 100) {
        volume = 100;
    }
    s_status.volume = volume;
    if (s_output_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = esp_codec_dev_set_out_vol(s_output_dev, volume);
    remember_error(err);
    return err;
}

esp_err_t atlas_audio_play_beep(uint16_t frequency_hz, uint16_t duration_ms, uint8_t volume)
{
    if (s_output_dev == NULL || !s_status.output_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (frequency_hz < 80) {
        frequency_hz = 80;
    }
    if (frequency_hz > 3000) {
        frequency_hz = 3000;
    }
    if (duration_ms < 40) {
        duration_ms = 40;
    }
    if (duration_ms > 1200) {
        duration_ms = 1200;
    }
    if (volume > 100) {
        volume = 100;
    }

    ESP_RETURN_ON_ERROR(ensure_lock(), TAG, "audio mutex failed");
    if (xSemaphoreTake(s_audio_lock, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    (void)esp_codec_dev_set_out_mute(s_output_dev, false);
    (void)esp_codec_dev_set_out_vol(s_output_dev, volume);

    int16_t frame[ATLAS_AUDIO_BEEP_CHUNK_FRAMES * ATLAS_AUDIO_CHANNELS];
    const uint32_t total_frames = (ATLAS_AUDIO_SAMPLE_RATE * (uint32_t)duration_ms) / 1000u;
    const uint32_t fade_frames = total_frames < 320u ? (total_frames / 4u) : 160u;
    const uint32_t phase_step = ((uint32_t)frequency_hz * 64u * 65536u) / ATLAS_AUDIO_SAMPLE_RATE;
    uint32_t phase = 0;
    uint32_t written_frames = 0;
    esp_err_t err = ESP_OK;

    while (written_frames < total_frames && err == ESP_OK) {
        const uint32_t todo = (total_frames - written_frames) > ATLAS_AUDIO_BEEP_CHUNK_FRAMES ?
                                ATLAS_AUDIO_BEEP_CHUNK_FRAMES :
                                (total_frames - written_frames);
        for (uint32_t i = 0; i < todo; ++i) {
            uint32_t global_frame = written_frames + i;
            int32_t amp = (int32_t)volume * 70;
            if (fade_frames > 0u && global_frame < fade_frames) {
                amp = (amp * (int32_t)global_frame) / (int32_t)fade_frames;
            }
            const uint32_t remaining = total_frames - global_frame;
            if (fade_frames > 0u && remaining < fade_frames) {
                amp = (amp * (int32_t)remaining) / (int32_t)fade_frames;
            }
            const int32_t sample = ((int32_t)s_sine_64[(phase >> 16) & 63u] * amp) / 32767;
            frame[i * 2] = (int16_t)sample;
            frame[i * 2 + 1] = (int16_t)sample;
            phase += phase_step;
        }
        memset(&frame[todo * ATLAS_AUDIO_CHANNELS],
               0,
               sizeof(frame) - (todo * ATLAS_AUDIO_CHANNELS * sizeof(frame[0])));
        err = esp_codec_dev_write(s_output_dev, frame, todo * ATLAS_AUDIO_CHANNELS * sizeof(frame[0]));
        written_frames += todo;
    }

    (void)esp_codec_dev_set_out_vol(s_output_dev, s_status.volume);
    if (err == ESP_OK) {
        s_status.speaker_tests++;
    } else {
        remember_error(err);
    }
    xSemaphoreGive(s_audio_lock);
    return err;
}

esp_err_t atlas_audio_measure_mic(uint16_t duration_ms, atlas_audio_mic_level_t *level)
{
    if (level == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(level, 0, sizeof(*level));
    if (s_input_dev == NULL || !s_status.input_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (duration_ms < 80) {
        duration_ms = 80;
    }
    if (duration_ms > 1200) {
        duration_ms = 1200;
    }

    ESP_RETURN_ON_ERROR(ensure_lock(), TAG, "audio mutex failed");
    if (xSemaphoreTake(s_audio_lock, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    int16_t samples[ATLAS_AUDIO_MIC_CHUNK_FRAMES * ATLAS_AUDIO_CHANNELS];
    const uint32_t target_frames = (ATLAS_AUDIO_SAMPLE_RATE * (uint32_t)duration_ms) / 1000u;
    uint32_t captured_frames = 0;
    uint64_t sum_squares = 0;
    uint32_t peak = 0;
    uint32_t sample_count = 0;
    esp_err_t err = ESP_OK;

    while (captured_frames < target_frames && err == ESP_OK) {
        const uint32_t todo = (target_frames - captured_frames) > ATLAS_AUDIO_MIC_CHUNK_FRAMES ?
                                ATLAS_AUDIO_MIC_CHUNK_FRAMES :
                                (target_frames - captured_frames);
        err = esp_codec_dev_read(s_input_dev, samples, todo * ATLAS_AUDIO_CHANNELS * sizeof(samples[0]));
        if (err != ESP_OK) {
            break;
        }
        const uint32_t count = todo * ATLAS_AUDIO_CHANNELS;
        for (uint32_t i = 0; i < count; ++i) {
            const int32_t value = samples[i];
            const uint32_t abs_value = value < 0 ? (uint32_t)(-value) : (uint32_t)value;
            if (abs_value > peak) {
                peak = abs_value;
            }
            sum_squares += (uint64_t)((int64_t)value * (int64_t)value);
        }
        sample_count += count;
        captured_frames += todo;
    }

    if (err == ESP_OK && sample_count > 0) {
        const uint32_t rms = isqrt_u64(sum_squares / sample_count);
        uint32_t scaled = (rms * 100u) / 10000u;
        if (peak > 18000u && scaled < 95u) {
            scaled = 95u;
        }
        if (scaled > 100u) {
            scaled = 100u;
        }
        level->level = (uint8_t)scaled;
        level->rms = rms;
        level->peak = peak;
        level->samples = sample_count;
        s_status.last_mic_level = level->level;
        s_status.last_mic_rms = rms;
        s_status.last_mic_peak = peak;
        s_status.mic_tests++;
    } else {
        remember_error(err);
    }

    xSemaphoreGive(s_audio_lock);
    return err;
}

esp_err_t atlas_audio_capture_pcm_mono(int16_t *mono_samples,
                                       size_t sample_count,
                                       atlas_audio_mic_level_t *level)
{
    if (mono_samples == NULL || sample_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (level != NULL) {
        memset(level, 0, sizeof(*level));
    }
    if (s_input_dev == NULL || !s_status.input_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(ensure_lock(), TAG, "audio mutex failed");
    if (xSemaphoreTake(s_audio_lock, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    int16_t stereo[ATLAS_AUDIO_MIC_CHUNK_FRAMES * ATLAS_AUDIO_CHANNELS];
    size_t captured = 0;
    uint64_t sum_squares = 0;
    uint32_t peak = 0;
    esp_err_t err = ESP_OK;

    while (captured < sample_count && err == ESP_OK) {
        const size_t todo = (sample_count - captured) > ATLAS_AUDIO_MIC_CHUNK_FRAMES ?
                                ATLAS_AUDIO_MIC_CHUNK_FRAMES :
                                (sample_count - captured);
        err = esp_codec_dev_read(s_input_dev, stereo, todo * ATLAS_AUDIO_CHANNELS * sizeof(stereo[0]));
        if (err != ESP_OK) {
            break;
        }
        for (size_t i = 0; i < todo; ++i) {
            const int32_t left = stereo[i * 2u];
            const int32_t right = stereo[i * 2u + 1u];
            const int16_t mono = (int16_t)((left + right) / 2);
            mono_samples[captured + i] = mono;
            const uint32_t abs_value = mono < 0 ? (uint32_t)(-((int32_t)mono)) : (uint32_t)mono;
            if (abs_value > peak) {
                peak = abs_value;
            }
            sum_squares += (uint64_t)((int64_t)mono * (int64_t)mono);
        }
        captured += todo;
    }

    if (err == ESP_OK && captured > 0 && level != NULL) {
        const uint32_t rms = isqrt_u64(sum_squares / captured);
        uint32_t scaled = (rms * 100u) / 10000u;
        if (peak > 18000u && scaled < 95u) {
            scaled = 95u;
        }
        if (scaled > 100u) {
            scaled = 100u;
        }
        level->level = (uint8_t)scaled;
        level->rms = rms;
        level->peak = peak;
        level->samples = (uint32_t)captured;
        s_status.last_mic_level = (uint8_t)scaled;
        s_status.last_mic_rms = rms;
        s_status.last_mic_peak = peak;
        s_status.mic_tests++;
    } else if (err != ESP_OK) {
        remember_error(err);
    }

    xSemaphoreGive(s_audio_lock);
    return err;
}

esp_err_t atlas_audio_capture_wav(uint16_t duration_ms,
                                  uint8_t **wav_data,
                                  size_t *wav_size,
                                  atlas_audio_mic_level_t *level)
{
    if (wav_data == NULL || wav_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *wav_data = NULL;
    *wav_size = 0;
    if (level != NULL) {
        memset(level, 0, sizeof(*level));
    }
    if (s_input_dev == NULL || !s_status.input_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (duration_ms < 400) {
        duration_ms = 400;
    }
    if (duration_ms > 6000) {
        duration_ms = 6000;
    }

    const uint32_t target_frames = (ATLAS_AUDIO_SAMPLE_RATE * (uint32_t)duration_ms) / 1000u;
    const uint32_t data_bytes = target_frames * sizeof(int16_t);
    const size_t total_bytes = ATLAS_AUDIO_WAV_HEADER_BYTES + (size_t)data_bytes;
    uint8_t *buffer = (uint8_t *)audio_malloc(total_bytes);
    if (buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }
    write_wav_header(buffer, ATLAS_AUDIO_SAMPLE_RATE, 1, ATLAS_AUDIO_BITS, data_bytes);

    ESP_RETURN_ON_ERROR(ensure_lock(), TAG, "audio mutex failed");
    if (xSemaphoreTake(s_audio_lock, pdMS_TO_TICKS(2000)) != pdTRUE) {
        free(buffer);
        return ESP_ERR_TIMEOUT;
    }

    int16_t samples[ATLAS_AUDIO_MIC_CHUNK_FRAMES * ATLAS_AUDIO_CHANNELS];
    uint8_t *out = buffer + ATLAS_AUDIO_WAV_HEADER_BYTES;
    uint32_t captured_frames = 0;
    uint64_t sum_squares = 0;
    uint32_t peak = 0;
    uint32_t sample_count = 0;
    esp_err_t err = ESP_OK;

    while (captured_frames < target_frames && err == ESP_OK) {
        const uint32_t todo = (target_frames - captured_frames) > ATLAS_AUDIO_MIC_CHUNK_FRAMES ?
                                ATLAS_AUDIO_MIC_CHUNK_FRAMES :
                                (target_frames - captured_frames);
        err = esp_codec_dev_read(s_input_dev, samples, todo * ATLAS_AUDIO_CHANNELS * sizeof(samples[0]));
        if (err != ESP_OK) {
            break;
        }
        for (uint32_t i = 0; i < todo; ++i) {
            const int32_t left = samples[i * 2];
            const int32_t right = samples[i * 2 + 1];
            const int16_t mono = (int16_t)((left + right) / 2);
            write_le16(out + ((captured_frames + i) * sizeof(int16_t)), (uint16_t)mono);
            const uint32_t abs_value = mono < 0 ? (uint32_t)(-((int32_t)mono)) : (uint32_t)mono;
            if (abs_value > peak) {
                peak = abs_value;
            }
            sum_squares += (uint64_t)((int64_t)mono * (int64_t)mono);
            sample_count++;
        }
        captured_frames += todo;
    }

    if (err == ESP_OK && sample_count > 0) {
        const uint32_t rms = isqrt_u64(sum_squares / sample_count);
        uint32_t scaled = (rms * 100u) / 10000u;
        if (peak > 18000u && scaled < 95u) {
            scaled = 95u;
        }
        if (scaled > 100u) {
            scaled = 100u;
        }
        if (level != NULL) {
            level->level = (uint8_t)scaled;
            level->rms = rms;
            level->peak = peak;
            level->samples = sample_count;
        }
        s_status.last_mic_level = (uint8_t)scaled;
        s_status.last_mic_rms = rms;
        s_status.last_mic_peak = peak;
        s_status.mic_tests++;
        *wav_data = buffer;
        *wav_size = total_bytes;
    } else {
        remember_error(err);
        free(buffer);
    }

    xSemaphoreGive(s_audio_lock);
    return err;
}

esp_err_t atlas_audio_play_wav_pcm(const uint8_t *wav_data, size_t wav_size, uint8_t volume)
{
    if (wav_data == NULL || wav_size < ATLAS_AUDIO_WAV_HEADER_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_output_dev == NULL || !s_status.output_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (memcmp(wav_data, "RIFF", 4) != 0 || memcmp(wav_data + 8, "WAVE", 4) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t *fmt = NULL;
    uint32_t fmt_size = 0;
    const uint8_t *data = NULL;
    uint32_t data_size = 0;
    size_t cursor = 12;
    while (cursor + 8 <= wav_size) {
        const uint8_t *chunk = wav_data + cursor;
        const uint32_t chunk_size = read_le32(chunk + 4);
        const size_t data_start = cursor + 8;
        if (data_start > wav_size || chunk_size > wav_size - data_start) {
            break;
        }
        if (memcmp(chunk, "fmt ", 4) == 0) {
            fmt = wav_data + data_start;
            fmt_size = chunk_size;
        } else if (memcmp(chunk, "data", 4) == 0) {
            data = wav_data + data_start;
            data_size = chunk_size;
        }
        cursor = data_start + chunk_size + (chunk_size & 1u);
    }

    if (fmt == NULL || fmt_size < 16 || data == NULL || data_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint16_t audio_format = read_le16(fmt);
    const uint16_t channels = read_le16(fmt + 2);
    const uint32_t sample_rate = read_le32(fmt + 4);
    const uint16_t bits = read_le16(fmt + 14);
    if (audio_format != 1 || bits != 16 || (channels != 1 && channels != 2)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (sample_rate != ATLAS_AUDIO_SAMPLE_RATE) {
        ESP_LOGW(TAG, "unsupported wav sample rate: %" PRIu32, sample_rate);
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (volume > 100) {
        volume = 100;
    }
    ESP_RETURN_ON_ERROR(ensure_lock(), TAG, "audio mutex failed");
    if (xSemaphoreTake(s_audio_lock, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    (void)esp_codec_dev_set_out_mute(s_output_dev, false);
    (void)esp_codec_dev_set_out_vol(s_output_dev, volume);

    int16_t frame[ATLAS_AUDIO_WAV_PLAY_CHUNK_FRAMES * ATLAS_AUDIO_CHANNELS];
    const uint32_t bytes_per_frame = channels * sizeof(int16_t);
    const uint32_t total_frames = data_size / bytes_per_frame;
    const uint32_t fade_frames = total_frames < (ATLAS_AUDIO_WAV_FADE_FRAMES * 2u) ?
                                    (total_frames / 4u) :
                                    ATLAS_AUDIO_WAV_FADE_FRAMES;
    uint32_t played_frames = 0;
    esp_err_t err = ESP_OK;
    while (played_frames < total_frames && err == ESP_OK) {
        const uint32_t todo = (total_frames - played_frames) > ATLAS_AUDIO_WAV_PLAY_CHUNK_FRAMES ?
                                ATLAS_AUDIO_WAV_PLAY_CHUNK_FRAMES :
                                (total_frames - played_frames);
        const uint8_t *src = data + (played_frames * bytes_per_frame);
        for (uint32_t i = 0; i < todo; ++i) {
            const uint32_t global_frame = played_frames + i;
            int32_t gain = 32767;
            if (channels == 1) {
                const int16_t mono = (int16_t)read_le16(src + i * sizeof(int16_t));
                frame[i * 2] = mono;
                frame[i * 2 + 1] = mono;
            } else {
                frame[i * 2] = (int16_t)read_le16(src + i * bytes_per_frame);
                frame[i * 2 + 1] = (int16_t)read_le16(src + i * bytes_per_frame + sizeof(int16_t));
            }
            if (fade_frames > 0u && global_frame < fade_frames) {
                gain = (gain * (int32_t)global_frame) / (int32_t)fade_frames;
            }
            const uint32_t remaining = total_frames - global_frame;
            if (fade_frames > 0u && remaining < fade_frames) {
                gain = (gain * (int32_t)remaining) / (int32_t)fade_frames;
            }
            frame[i * 2] = (int16_t)(((int32_t)frame[i * 2] * gain) / 32767);
            frame[i * 2 + 1] = (int16_t)(((int32_t)frame[i * 2 + 1] * gain) / 32767);
        }
        err = esp_codec_dev_write(s_output_dev, frame, todo * ATLAS_AUDIO_CHANNELS * sizeof(frame[0]));
        played_frames += todo;
    }

    if (err == ESP_OK) {
        memset(frame, 0, sizeof(frame));
        uint32_t silence_frames = ATLAS_AUDIO_WAV_TAIL_SILENCE_FRAMES;
        while (silence_frames > 0u && err == ESP_OK) {
            const uint32_t todo = silence_frames > ATLAS_AUDIO_WAV_PLAY_CHUNK_FRAMES ?
                                    ATLAS_AUDIO_WAV_PLAY_CHUNK_FRAMES :
                                    silence_frames;
            err = esp_codec_dev_write(s_output_dev, frame, todo * ATLAS_AUDIO_CHANNELS * sizeof(frame[0]));
            silence_frames -= todo;
        }
        (void)esp_codec_dev_set_out_mute(s_output_dev, true);
    }
    (void)esp_codec_dev_set_out_vol(s_output_dev, s_status.volume);
    if (err == ESP_OK) {
        s_status.speaker_tests++;
    } else {
        remember_error(err);
    }
    xSemaphoreGive(s_audio_lock);
    return err;
}

void atlas_audio_get_status(atlas_audio_status_t *status)
{
    if (status == NULL) {
        return;
    }
    *status = s_status;
}
