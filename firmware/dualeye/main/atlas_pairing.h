#pragma once

#include <stdbool.h>

void atlas_pairing_init(void);
const char *atlas_pairing_code(void);
bool atlas_pairing_authorize_pin(const char *pin);
