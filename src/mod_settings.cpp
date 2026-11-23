#include "mod_settings.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace rbrew_mod_settings {

void default_key_bindings(int (&keys)[kActionCount]) {
    for (int i = 0; i < kActionCount; ++i) keys[i] = kUnbound;
    keys[kActA] = 40;
    keys[kActB] = 44;
    keys[kActX] = 20;
    keys[kActY] = 8;
    keys[kActR] = 21;
    keys[kActPlus] = 9;
    keys[kActMinus] = 10;
    keys[kActHome] = 11;
    keys[kActZR] = kMouseBase + 1;
    keys[kActZL] = kMouseBase + 3;
    keys[kActMoveUp] = 26;
    keys[kActMoveDown] = 22;
    keys[kActMoveLeft] = 4;
    keys[kActMoveRight] = 7;
    keys[kActStickR] = kMouseBase + 2;
    keys[kActStickL] = 6;
    keys[kActL] = 29;
    keys[kActUp] = 30;
    keys[kActDown] = 31;
    keys[kActLeft] = 32;
    keys[kActRight] = 33;
}

namespace {

const char* const kActionKeys[kActionCount] = {
    "a",  "b",    "x",    "y",     "l",       "r",       "zl",      "zr",        "plus",      "minus",     "home",
    "up", "down", "left", "right", "stick_l", "stick_r", "move_up", "move_down", "move_left", "move_right"};

std::mutex s_lock;
Values s_values;
std::filesystem::path s_ini_path;

std::filesystem::path exe_dir() {
#if defined(_WIN32)
    wchar_t module[MAX_PATH * 4]{};
    const DWORD n = GetModuleFileNameW(nullptr, module, DWORD(std::size(module)));
    if (n > 0 && n < std::size(module)) return std::filesystem::path(module).parent_path();
#else
    char module[4096]{};
    const ssize_t n = readlink("/proc/self/exe", module, sizeof(module) - 1);
    if (n > 0) return std::filesystem::path(std::string(module, size_t(n))).parent_path();
#endif
    return {};
}

std::filesystem::path host_settings_path() {
    const auto dir = exe_dir();
    return dir.empty() ? std::filesystem::path("settings.ini") : (dir / "settings.ini");
}

std::filesystem::path mod_settings_path(const char* mods_dir) {
    if (mods_dir && *mods_dir) {
        return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(mods_dir), std::strlen(mods_dir))) /
               "online_coop.ini";
    }
    const auto dir = exe_dir();
    return dir.empty() ? std::filesystem::path("mods/online_coop.ini") : (dir / "mods" / "online_coop.ini");
}

void publish_locked() {
    const Values& v = s_values;
    g_coop_story.store(v.coop_story, std::memory_order_relaxed);
    g_volume.store(v.volume / 100.0f, std::memory_order_relaxed);
    g_mouse_scale.store(v.mouse_sensitivity / 100.0f, std::memory_order_relaxed);
    for (int i = 0; i < kActionCount; ++i) g_keys[i].store(v.keys[i], std::memory_order_relaxed);
}

bool parse_int(const std::string& s, int& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (end != s.c_str() + s.size()) return false;
    out = int(v);
    return true;
}

void read_ini(const std::filesystem::path& path, Values& v, bool read_host_keys) {
    std::ifstream in(path);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        int number = 0;
        const bool is_num = parse_int(value, number);
        if (key == "net_port" && is_num)
            v.net_port = number;
        else if (key == "net_upnp" && is_num)
            v.net_upnp = number != 0;
        else if (key == "net_kbps" && is_num)
            v.net_kbps = number;
        else if (key == "net_join")
            v.net_join = value;
        else if (key == "coop_story" && is_num)
            v.coop_story = number != 0;
        else if (read_host_keys && key == "volume" && is_num)
            v.volume = number;
        else if (read_host_keys && key == "mouse_sensitivity" && is_num)
            v.mouse_sensitivity = number;
        else if (read_host_keys && is_num && key.rfind("key_", 0) == 0) {
            for (int i = 0; i < kActionCount; ++i) {
                if (key == std::string("key_") + kActionKeys[i]) v.keys[i] = number;
            }
        }
    }
}

void write_ini(const std::filesystem::path& path, const Values& v) {
    if (path.empty()) return;
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::ostringstream out;
    out << "net_port=" << v.net_port << "\n"
        << "net_upnp=" << (v.net_upnp ? 1 : 0) << "\n"
        << "net_kbps=" << v.net_kbps << "\n"
        << "net_join=" << v.net_join << "\n"
        << "coop_story=" << (v.coop_story ? 1 : 0) << "\n";
    std::ofstream f(path, std::ios::trunc);
    if (!f) return;
    f << out.str();
}

} // namespace

void normalize(Values& v) {
    v.net_port = std::clamp(v.net_port, 1024, 65535);
    v.net_kbps = std::clamp(v.net_kbps, 1000, 50000);
    v.mouse_sensitivity = std::clamp(v.mouse_sensitivity, 25, 400);
    v.volume = std::clamp(v.volume, 0, 100);
    std::erase_if(v.net_join,
                  [](char c) { return !std::isalnum(static_cast<unsigned char>(c)) && !std::strchr(".-:", c); });
    if (v.net_join.size() > 253) v.net_join.clear();
}

void load(const char* mods_dir) {
    std::lock_guard lock(s_lock);
    s_values = Values{};
    s_ini_path = mod_settings_path(mods_dir);

    // Pick up host volume, mouse sensitivity, and keyboard bindings, plus any
    // legacy net_* keys.
    read_ini(host_settings_path(), s_values, true);
    // Mod's own ini wins for mod settings.
    read_ini(s_ini_path, s_values, false);

    normalize(s_values);
    publish_locked();
}

Values get() {
    std::lock_guard lock(s_lock);
    return s_values;
}

void set(const Values& values) {
    std::lock_guard lock(s_lock);
    s_values = values;
    normalize(s_values);
    publish_locked();
    write_ini(s_ini_path, s_values);
}

} // namespace rbrew_mod_settings
