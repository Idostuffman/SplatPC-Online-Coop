#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace rbrew_mod_settings {

enum Action : int {
    kActA,
    kActB,
    kActX,
    kActY,
    kActL,
    kActR,
    kActZL,
    kActZR,
    kActPlus,
    kActMinus,
    kActHome,
    kActUp,
    kActDown,
    kActLeft,
    kActRight,
    kActStickL,
    kActStickR,
    kActMoveUp,
    kActMoveDown,
    kActMoveLeft,
    kActMoveRight,
    kActionCount
};

constexpr int kFirstMoveAction = kActMoveUp;
constexpr int kUnbound = -1;
constexpr int kMouseBase = 1000;

void default_key_bindings(int (&keys)[kActionCount]);

struct Values {
    int net_port = 52815;
    bool net_upnp = true;
    int net_kbps = 10000;
    std::string net_join;
    bool coop_story = true;

    // Guest-window preferences (inherited read-only from host settings.ini).
    int volume = 100;
    int mouse_sensitivity = 100;
    int keys[kActionCount];

    Values() { default_key_bindings(keys); }
};

void load(const char* mods_dir = nullptr);
Values get();
void set(const Values& values);
void normalize(Values& values);

inline std::atomic<bool> g_coop_story{true};
inline std::atomic<float> g_volume{1.0f};
inline std::atomic<float> g_mouse_scale{1.0f};
inline std::atomic<int> g_keys[kActionCount];

} // namespace rbrew_mod_settings
