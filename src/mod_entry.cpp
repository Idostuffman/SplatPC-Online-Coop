#include "coop_hooks.h"
#include "coop_views.h"
#include "mod_log.h"
#include "mod_settings.h"
#include "netplay.h"
#include "netplay_connect.h"
#include "online_menu.h"
#include "splat_mod_api.h"

namespace {

const SplatModHost* g_host = nullptr;
SplatModHookHandle g_menu_handle = 0;
SplatModHookHandle g_vpad_handle = 0;
SplatModHookHandle g_kpad_handle = 0;
SplatModHookHandle g_audio_handle = 0;
SplatModHookHandle g_capture_handle = 0;

bool g_prev_coop_title = false;
bool g_prev_dojo_mode = false;
bool g_dojo_auto_switched = false;

void on_vpad_tick(uint8_t* mem, void* /*user*/) {
    rbrew_coop::bump_frame();
    rbrew_coop::update_views(mem);
    if (!g_host) return;

    const bool two = rbrew_coop::two_views();
    const bool dojo = rbrew_coop::dojo_match();
    const bool connected = rbrew_net::host_guest_connected();

    g_host->set_dual_view_active(two);
    g_host->set_suppress_cinema_hud(connected);

    const bool in_coop = two || (dojo && rbrew_coop::second_player_present());
    if (in_coop != g_prev_coop_title) {
        g_prev_coop_title = in_coop;
        g_host->set_window_title(in_coop ? "Splatoon (P1) | Vulkan [F3=overlay | Tab=view | F11=window]" : nullptr);
    }

    if (dojo != g_prev_dojo_mode) {
        g_prev_dojo_mode = dojo;
        if (dojo) {
            if (g_host->current_screen_mode() == SPLATMOD_SCREEN_TV && rbrew_coop::second_player_present()) {
                g_host->request_screen_mode(SPLATMOD_SCREEN_GAMEPAD);
                g_dojo_auto_switched = true;
            }
            g_host->set_relative_mouse_in_gamepad(true);
        } else {
            if (g_dojo_auto_switched && g_host->current_screen_mode() == SPLATMOD_SCREEN_GAMEPAD) {
                g_host->request_screen_mode(SPLATMOD_SCREEN_TV);
            }
            g_dojo_auto_switched = false;
            g_host->set_relative_mouse_in_gamepad(false);
        }
    }
}

bool on_kpad_poll(uint32_t chan, SplatModProPad* out_pad, void* /*user*/) {
    if (chan != 0 || !out_pad) return false;
    rbrew_net::ProPad pad;
    if (!rbrew_net::host_remote_pad(pad)) return false;
    out_pad->hold = pad.hold;
    out_pad->lx = pad.lx;
    out_pad->ly = pad.ly;
    out_pad->rx = pad.rx;
    out_pad->ry = pad.ry;
    return true;
}

void on_audio_tap(const float* stereo_frames, uint32_t frame_count, void* /*user*/) {
    rbrew_net::host_submit_audio(stereo_frames, frame_count);
}

bool on_capture_query(uint32_t* out_view, uint32_t* out_width, uint32_t* out_height, void* /*user*/) {
    if (!out_view || !out_width || !out_height) return false;
    if (!rbrew_net::host_wants_video()) return false;
    *out_view = rbrew_coop::two_views() ? SPLATMOD_SCREEN_GAMEPAD : SPLATMOD_SCREEN_TV;
    *out_width = 0;
    *out_height = 0;
    return true;
}

void on_capture_submit(const uint8_t* rgba, uint32_t width, uint32_t height, void* /*user*/) {
    rbrew_net::host_submit_frame(rgba, width, height);
}

const SplatModInfo kModInfo = {
    SPLATMOD_ABI_VERSION, "splatpc.online_coop", "Online Co-op", "1.0.0", "SplatPC",
};

} // namespace

SPLATMOD_EXPORT const SplatModInfo* splatmod_info(void) {
    return &kModInfo;
}

SPLATMOD_EXPORT int splatmod_early_init(const SplatModEarlyHost* host, int argc, char** argv) {
    if (!host || host->abi_version != SPLATMOD_ABI_VERSION) return -1;
    rbrew_mod_log::set_log_fn(host->log);
    rbrew_mod_settings::load(host->mods_dir ? host->mods_dir() : nullptr);

    const std::string join = rbrew_net::join_address(argc, argv);
    if (!join.empty()) return rbrew_net::run_guest(join);
    return -1;
}

SPLATMOD_EXPORT bool splatmod_init(const SplatModHost* host) {
    if (!host || host->abi_version != SPLATMOD_ABI_VERSION) return false;
    g_host = host;
    g_prev_coop_title = false;
    g_prev_dojo_mode = false;
    g_dojo_auto_switched = false;

    rbrew_mod_log::set_log_fn(host->log);
    rbrew_mod_settings::load(host->mods_dir ? host->mods_dir() : nullptr);
    rbrew_net::host_start_from_env();

    if (!coop_hooks::install(host)) {
        rbrew_net::host_stop();
        g_host = nullptr;
        return false;
    }

    g_menu_handle = host->add_menu_page("Online Co-op", &online_menu::draw_page, &online_menu::status_line,
                                        const_cast<SplatModHost*>(host));
    g_vpad_handle = host->add_vpad_hook(&on_vpad_tick, nullptr);
    g_kpad_handle = host->add_kpad_provider(&on_kpad_poll, nullptr);
    g_audio_handle = host->add_audio_tap(&on_audio_tap, nullptr);
    g_capture_handle = host->set_frame_capture(&on_capture_query, &on_capture_submit, nullptr);
    return g_menu_handle != 0 && g_vpad_handle != 0 && g_kpad_handle != 0 && g_audio_handle != 0 &&
           g_capture_handle != 0;
}

SPLATMOD_EXPORT void splatmod_shutdown(void) {
    rbrew_net::host_stop();
    rbrew_net::router_shutdown();
    coop_hooks::uninstall(g_host);

    if (g_host) {
        if (g_menu_handle) g_host->unhook_extension(g_menu_handle);
        if (g_vpad_handle) g_host->unhook_extension(g_vpad_handle);
        if (g_kpad_handle) g_host->unhook_extension(g_kpad_handle);
        if (g_audio_handle) g_host->unhook_extension(g_audio_handle);
        if (g_capture_handle) g_host->unhook_extension(g_capture_handle);
        g_host->set_dual_view_active(false);
        g_host->set_suppress_cinema_hud(false);
        g_host->set_relative_mouse_in_gamepad(false);
        g_host->set_window_title(nullptr);
    }

    g_menu_handle = g_vpad_handle = g_kpad_handle = g_audio_handle = g_capture_handle = 0;
    g_prev_coop_title = false;
    g_prev_dojo_mode = false;
    g_dojo_auto_switched = false;
    g_host = nullptr;
    rbrew_mod_log::set_log_fn(nullptr);
}
