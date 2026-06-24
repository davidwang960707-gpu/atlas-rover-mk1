#pragma once

#include <stdint.h>

#include "esp_err.h"

#include "atlas_config.h"
#include "atlas_display.h"
#include "atlas_ui.h"

typedef uint32_t (*atlas_admin_now_ms_fn_t)(void);

esp_err_t atlas_admin_http_start(atlas_config_t *config,
                                 atlas_ui_state_t *ui_state,
                                 atlas_admin_now_ms_fn_t now_ms_fn);
