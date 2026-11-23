#pragma once

#include "splat_mod_api.h"

namespace online_menu {

int draw_page(const SplatModUi* ui, void* user);
const char* status_line(void* user);

} // namespace online_menu
