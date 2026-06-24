#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "atlas_page.h"

#define ATLAS_COMMON_UI_STATE_PROTOCOL "atlas.ui.state.v0"
#define ATLAS_COMMON_UI_DEFAULT_CHAT_MODE "pet_head"
#define ATLAS_COMMON_UI_CHAT_MODE_TEXT "text"
#define ATLAS_COMMON_UI_CHAT_MODE_PET_HEAD "pet_head"
#define ATLAS_COMMON_UI_CHAT_MODE_EYES_ONLY "eyes_only"
#define ATLAS_COMMON_UI_MANUAL_PAGE_OVERRIDE_MS 8000u
#define ATLAS_COMMON_UI_TRANSIENT_RETURN_MS 3500u

const char *atlas_common_ui_state_protocol(void);
bool atlas_common_ui_chat_mode_is_valid(const char *mode);
const char *atlas_common_ui_chat_mode_or_default(const char *mode);

bool atlas_common_ui_page_is_app(atlas_page_t page);
bool atlas_common_ui_page_is_manual_display(atlas_page_t page);
bool atlas_common_ui_page_is_persistent(atlas_page_t page);
bool atlas_common_ui_page_uses_chat_mode(atlas_page_t page);

bool atlas_common_ui_recent_manual_page(atlas_page_t page,
                                        uint32_t last_event_ms,
                                        uint32_t now_ms);
