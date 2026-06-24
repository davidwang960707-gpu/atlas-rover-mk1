#include "common/atlas_common_ui_state.h"

#include <string.h>

const char *atlas_common_ui_state_protocol(void)
{
    return ATLAS_COMMON_UI_STATE_PROTOCOL;
}

bool atlas_common_ui_chat_mode_is_valid(const char *mode)
{
    return mode != NULL &&
           (strcmp(mode, ATLAS_COMMON_UI_CHAT_MODE_TEXT) == 0 ||
            strcmp(mode, ATLAS_COMMON_UI_CHAT_MODE_PET_HEAD) == 0 ||
            strcmp(mode, ATLAS_COMMON_UI_CHAT_MODE_EYES_ONLY) == 0);
}

const char *atlas_common_ui_chat_mode_or_default(const char *mode)
{
    return atlas_common_ui_chat_mode_is_valid(mode) ? mode : ATLAS_COMMON_UI_DEFAULT_CHAT_MODE;
}

bool atlas_common_ui_page_is_app(atlas_page_t page)
{
    return page == ATLAS_PAGE_CLOCK ||
           page == ATLAS_PAGE_MUSIC ||
           page == ATLAS_PAGE_STORY ||
           page == ATLAS_PAGE_CHAT ||
           page == ATLAS_PAGE_CALENDAR ||
           page == ATLAS_PAGE_POMODORO;
}

bool atlas_common_ui_page_is_manual_display(atlas_page_t page)
{
    return page == ATLAS_PAGE_EYES ||
           page == ATLAS_PAGE_CLOCK ||
           page == ATLAS_PAGE_STATUS ||
           page == ATLAS_PAGE_VOICE ||
           atlas_common_ui_page_is_app(page);
}

bool atlas_common_ui_page_is_persistent(atlas_page_t page)
{
    return page == ATLAS_PAGE_CLOCK ||
           page == ATLAS_PAGE_STATUS ||
           page == ATLAS_PAGE_ALARM ||
           page == ATLAS_PAGE_PHOTO ||
           page == ATLAS_PAGE_MUSIC ||
           page == ATLAS_PAGE_STORY ||
           page == ATLAS_PAGE_CHAT ||
           page == ATLAS_PAGE_CALENDAR ||
           page == ATLAS_PAGE_POMODORO;
}

bool atlas_common_ui_page_uses_chat_mode(atlas_page_t page)
{
    return page == ATLAS_PAGE_CHAT ||
           page == ATLAS_PAGE_VOICE ||
           page == ATLAS_PAGE_MUSIC ||
           page == ATLAS_PAGE_STORY;
}

bool atlas_common_ui_recent_manual_page(atlas_page_t page,
                                        uint32_t last_event_ms,
                                        uint32_t now_ms)
{
    return last_event_ms != 0u &&
           now_ms >= last_event_ms &&
           now_ms - last_event_ms <= ATLAS_COMMON_UI_MANUAL_PAGE_OVERRIDE_MS &&
           atlas_common_ui_page_is_manual_display(page);
}
