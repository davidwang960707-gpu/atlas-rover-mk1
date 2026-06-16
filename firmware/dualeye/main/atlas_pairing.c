#include "atlas_pairing.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"

static const char *TAG = "atlas_pairing";
static char s_pairing_code[7] = "000000";

void atlas_pairing_init(void)
{
    const uint32_t code = 100000u + (esp_random() % 900000u);
    snprintf(s_pairing_code, sizeof(s_pairing_code), "%06lu", (unsigned long)code);
    ESP_LOGW(TAG, "local management pairing code: %s", s_pairing_code);
    ESP_LOGW(TAG, "STOP endpoint stays available without pairing; movement/config endpoints require the code");
}

const char *atlas_pairing_code(void)
{
    return s_pairing_code;
}

bool atlas_pairing_authorize_pin(const char *pin)
{
    return pin != NULL && strncmp(pin, s_pairing_code, sizeof(s_pairing_code)) == 0;
}
