#pragma once

// Shared view state and per-frame coordination for Story Mode co-op & Battle
// Dojo.

#include <cstdint>

namespace rbrew_coop {

bool two_views();
bool dojo_match();
bool second_player_present();
void update_views(uint8_t* mem);
void bump_frame();
uint32_t current_frame();

} // namespace rbrew_coop
