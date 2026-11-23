#include "splat_mod_api.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::fprintf(stderr, "CHECK FAILED at line %d: %s\n", __LINE__, #cond);                                    \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (0)

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

struct MockState {
    uint64_t next_handle = 1;
    std::vector<std::pair<uint64_t, uint32_t>> hooked_guests;
    std::vector<uint64_t> active_extensions;

    SplatModMenuPageCallback menu_draw = nullptr;
    SplatModMenuStatusCallback menu_status = nullptr;
    void* menu_user = nullptr;

    SplatModVpadCallback vpad_cb = nullptr;
    void* vpad_user = nullptr;

    SplatModKpadProvider kpad_cb = nullptr;
    void* kpad_user = nullptr;

    SplatModAudioCallback audio_cb = nullptr;
    void* audio_user = nullptr;

    SplatModCaptureQuery capture_query = nullptr;
    SplatModCaptureSubmit capture_submit = nullptr;
    void* capture_user = nullptr;

    bool dual_view = false;
    bool suppress_cinema = false;
    bool relative_mouse = false;
    int screen_mode = SPLATMOD_SCREEN_TV;
    bool quit_requested = false;
    int ui_text_calls = 0;
};

MockState g_mock;

void mock_dummy_orig(CPUState* /*cpu*/) {}

void mock_log(SplatModLogLevel /*level*/, const char* /*tag*/, const char* /*fmt*/, ...) {}

const char* mock_mods_dir() {
    return "";
}

SplatModHookHandle mock_hook_guest(uint32_t guest_addr, SplatModGuestHook hook, SplatModGuestOrig* out_orig) {
    if (!hook) return 0;
    const uint64_t h = g_mock.next_handle++;
    g_mock.hooked_guests.emplace_back(h, guest_addr);
    if (out_orig) *out_orig = &mock_dummy_orig;
    return h;
}

bool mock_unhook_guest(SplatModHookHandle handle) {
    for (size_t i = 0; i < g_mock.hooked_guests.size(); ++i) {
        if (g_mock.hooked_guests[i].first == handle) {
            g_mock.hooked_guests.erase(g_mock.hooked_guests.begin() + ptrdiff_t(i));
            return true;
        }
    }
    return false;
}

SplatModHookHandle mock_hook_native(void*, void*, void**) {
    return 0;
}
bool mock_unhook_native(SplatModHookHandle) {
    return false;
}
bool mock_call_guest(CPUState*, uint32_t, const uint32_t*, uint32_t, uint32_t*) {
    return false;
}
int mock_active_scene_type(uint8_t*, int* out_id) {
    if (out_id) *out_id = -1;
    return -1;
}
bool mock_memory_accessible(const uint8_t*, uint32_t, uint64_t) {
    return false;
}
void mock_write_memory(uint8_t*, uint32_t, const void*, uint32_t) {}

SplatModHookHandle mock_add_menu_page(const char* /*title*/, SplatModMenuPageCallback draw,
                                      SplatModMenuStatusCallback status, void* user) {
    const uint64_t h = g_mock.next_handle++;
    g_mock.active_extensions.push_back(h);
    g_mock.menu_draw = draw;
    g_mock.menu_status = status;
    g_mock.menu_user = user;
    return h;
}

SplatModHookHandle mock_add_vpad_hook(SplatModVpadCallback cb, void* user) {
    const uint64_t h = g_mock.next_handle++;
    g_mock.active_extensions.push_back(h);
    g_mock.vpad_cb = cb;
    g_mock.vpad_user = user;
    return h;
}

SplatModHookHandle mock_add_kpad_provider(SplatModKpadProvider provider, void* user) {
    const uint64_t h = g_mock.next_handle++;
    g_mock.active_extensions.push_back(h);
    g_mock.kpad_cb = provider;
    g_mock.kpad_user = user;
    return h;
}

SplatModHookHandle mock_add_audio_tap(SplatModAudioCallback cb, void* user) {
    const uint64_t h = g_mock.next_handle++;
    g_mock.active_extensions.push_back(h);
    g_mock.audio_cb = cb;
    g_mock.audio_user = user;
    return h;
}

SplatModHookHandle mock_set_frame_capture(SplatModCaptureQuery query, SplatModCaptureSubmit submit, void* user) {
    const uint64_t h = g_mock.next_handle++;
    g_mock.active_extensions.push_back(h);
    g_mock.capture_query = query;
    g_mock.capture_submit = submit;
    g_mock.capture_user = user;
    return h;
}

bool mock_unhook_extension(SplatModHookHandle handle) {
    for (size_t i = 0; i < g_mock.active_extensions.size(); ++i) {
        if (g_mock.active_extensions[i] == handle) {
            g_mock.active_extensions.erase(g_mock.active_extensions.begin() + ptrdiff_t(i));
            return true;
        }
    }
    return false;
}

void mock_set_dual_view(bool a) {
    g_mock.dual_view = a;
}
void mock_set_suppress_cinema(bool s) {
    g_mock.suppress_cinema = s;
}
void mock_set_rel_mouse(bool e) {
    g_mock.relative_mouse = e;
}
void mock_set_title(const char*) {}
void mock_request_screen(int mode) {
    g_mock.screen_mode = mode;
}
int mock_current_screen() {
    return g_mock.screen_mode;
}
void mock_request_quit() {
    g_mock.quit_requested = true;
}

const SplatModUi kMockUi = {
    nullptr,
    [](void*, const char*) { g_mock.ui_text_calls++; },
    [](void*, float, float, float, float, const char*) { g_mock.ui_text_calls++; },
    [](void*, const char*) { g_mock.ui_text_calls++; },
    [](void*) {},
    [](void*, const char*) {},
    [](void*) {},
    [](void*) {},
    [](void*, const char*, float) { return false; },
    [](void*, const char*, bool*) { return false; },
    [](void*, const char*, int*, int, int, const char*) { return false; },
    [](void*, const char*, const char*, char*, size_t) { return false; },
    [](void*, bool) {},
    [](void*) {},
    [](void*, const char*) {},
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || !argv[1] || !*argv[1]) {
        std::fprintf(stderr, "Usage: online_coop_mod_test <path-to-online_coop-plugin>\n");
        return 2;
    }

#if defined(_WIN32)
    std::wstring wpath(std::strlen(argv[1]) + 1, L'\0');
    const int wn = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, wpath.data(), int(wpath.size()));
    CHECK(wn > 0);
    HMODULE lib = LoadLibraryExW(wpath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    CHECK(lib != nullptr);
    auto sym = [&](const char* name) { return reinterpret_cast<void*>(GetProcAddress(lib, name)); };
#else
    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    CHECK(lib != nullptr);
    auto sym = [&](const char* name) { return dlsym(lib, name); };
#endif

    auto info_fn = reinterpret_cast<SplatModInfoFn>(sym("splatmod_info"));
    auto early_fn = reinterpret_cast<SplatModEarlyInitFn>(sym("splatmod_early_init"));
    auto init_fn = reinterpret_cast<SplatModInitFn>(sym("splatmod_init"));
    auto shutdown_fn = reinterpret_cast<SplatModShutdownFn>(sym("splatmod_shutdown"));
    CHECK(info_fn && early_fn && init_fn && shutdown_fn);

    const SplatModInfo* info = info_fn();
    CHECK(info && info->abi_version == SPLATMOD_ABI_VERSION);
    CHECK(info->id && std::strcmp(info->id, "splatpc.online_coop") == 0);
    CHECK(info->name && std::strcmp(info->name, "Online Co-op") == 0);

    const SplatModEarlyHost early_host{SPLATMOD_ABI_VERSION, sizeof(SplatModEarlyHost), 288u, &mock_log,
                                       &mock_mods_dir};
    char arg0[] = "Splatoon.exe";
    char* fake_argv[] = {arg0, nullptr};
    CHECK(early_fn(&early_host, 1, fake_argv) == -1);

    const SplatModHost host = {
        SPLATMOD_ABI_VERSION,
        sizeof(SplatModHost),
        288u,
        &mock_log,
        &mock_mods_dir,
        &mock_hook_guest,
        &mock_unhook_guest,
        &mock_hook_native,
        &mock_unhook_native,
        &mock_call_guest,
        &mock_active_scene_type,
        &mock_memory_accessible,
        &mock_write_memory,
        &mock_add_menu_page,
        &mock_add_vpad_hook,
        &mock_add_kpad_provider,
        &mock_add_audio_tap,
        &mock_set_frame_capture,
        &mock_unhook_extension,
        &mock_set_dual_view,
        &mock_set_suppress_cinema,
        &mock_set_rel_mouse,
        &mock_set_title,
        &mock_request_screen,
        &mock_current_screen,
        &mock_request_quit,
    };

    CHECK(init_fn(&host));

    // Verify all 16 Story Mode / Battle Dojo v288 guest hooks were registered.
    const uint32_t expected_addrs[] = {
        0x0265A730u, 0x023FE9C8u, 0x023FED60u, 0x026BA97Cu, 0x026BA904u, 0x0289F358u, 0x02A3F070u, 0x029E7B00u,
        0x029E9608u, 0x0240B07Cu, 0x023FF448u, 0x02652E84u, 0x02A96208u, 0x02783D90u, 0x0248B5E8u, 0x023FCBBCu,
    };
    CHECK(g_mock.hooked_guests.size() == std::size(expected_addrs));
    for (uint32_t addr : expected_addrs) {
        const bool found = std::any_of(g_mock.hooked_guests.begin(), g_mock.hooked_guests.end(),
                                       [addr](const auto& pair) { return pair.second == addr; });
        CHECK(found);
    }

    CHECK(g_mock.active_extensions.size() == 5);
    CHECK(g_mock.menu_draw && g_mock.vpad_cb && g_mock.kpad_cb && g_mock.audio_cb && g_mock.capture_query &&
          g_mock.capture_submit);

    CHECK(g_mock.menu_draw(&kMockUi, g_mock.menu_user) == SPLATMOD_MENU_STAY);
    CHECK(g_mock.ui_text_calls > 0);
    CHECK(g_mock.menu_status && g_mock.menu_status(g_mock.menu_user) != nullptr);

    uint8_t dummy_mem[64]{};
    g_mock.vpad_cb(dummy_mem, g_mock.vpad_user);

    SplatModProPad pad{};
    CHECK(!g_mock.kpad_cb(0u, &pad, g_mock.kpad_user));

    float stereo[32]{};
    g_mock.audio_cb(stereo, 16u, g_mock.audio_user);

    uint32_t view = 0, w = 0, h = 0;
    CHECK(!g_mock.capture_query(&view, &w, &h, g_mock.capture_user));

    shutdown_fn();
    CHECK(g_mock.hooked_guests.empty());
    CHECK(g_mock.active_extensions.empty());

#if defined(_WIN32)
    FreeLibrary(lib);
#else
    dlclose(lib);
#endif

    std::printf("online_coop_mod_test: all assertions passed\n");
    return 0;
}
