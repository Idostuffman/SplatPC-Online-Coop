#include "online_menu.h"

#include "mod_settings.h"
#include "netplay.h"
#include "netplay_connect.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace online_menu {
namespace {

namespace rs = rbrew_mod_settings;
namespace net = rbrew_net;
using RouterState = net::RouterStatus::State;

char s_port_buf[16] = "";
bool s_port_loaded = false;
char s_join[256] = "";
bool s_join_loaded = false;
bool s_confirm_join = false;
bool s_show_manual_help = false;
bool s_show_stream = false;
std::string s_message;
std::string s_local_ip;
std::chrono::steady_clock::time_point s_local_ip_checked = std::chrono::steady_clock::time_point::min();

void text_good(const SplatModUi* ui, const char* text) {
    ui->text_colored(ui->user, 0.35f, 0.95f, 0.45f, 1.0f, text);
}

void text_wait(const SplatModUi* ui, const char* text) {
    ui->text_colored(ui->user, 0.95f, 0.85f, 0.35f, 1.0f, text);
}

void text_bad(const SplatModUi* ui, const char* text) {
    ui->text_colored(ui->user, 0.95f, 0.4f, 0.4f, 1.0f, text);
}

void text_muted(const SplatModUi* ui, const char* text) {
    ui->text_colored(ui->user, 0.65f, 0.65f, 0.68f, 1.0f, text);
}

void address_row(const SplatModUi* ui, const char* label, const std::string& address, const char* id) {
    ui->text(ui->user, label);
    ui->same_line(ui->user);
    text_good(ui, address.c_str());

    const std::string copy_btn = std::string("Copy##") + id;
    const std::string join_btn = std::string("Copy --join##") + id;
    ui->same_line(ui->user);
    if (ui->button(ui->user, copy_btn.c_str(), 0.0f) && ui->set_clipboard_text) {
        ui->set_clipboard_text(ui->user, address.c_str());
    }
    ui->same_line(ui->user);
    if (ui->button(ui->user, join_btn.c_str(), 0.0f) && ui->set_clipboard_text) {
        const std::string cmd = "Splatoon.exe --join " + address;
        ui->set_clipboard_text(ui->user, cmd.c_str());
    }
}

void router_line(const SplatModUi* ui, const net::RouterStatus& r, uint16_t port) {
    char buf[256];
    switch (r.state) {
    case RouterState::Off:
        text_muted(ui, "Automatic port forwarding is off.");
        ui->same_line(ui->user);
        if (ui->button(ui->user, "Open the port now", 0.0f)) net::router_open(port);
        break;
    case RouterState::Working:
        std::snprintf(buf, sizeof(buf), "Asking your router to open port %u ...", unsigned(port));
        text_wait(ui, buf);
        break;
    case RouterState::Open:
        std::snprintf(buf, sizeof(buf), "Port %u is open on your router.", unsigned(port));
        text_good(ui, buf);
        break;
    case RouterState::Failed:
        text_bad(ui, "Automatic port forwarding failed:");
        std::snprintf(buf, sizeof(buf), "%s Forward the port by hand (see below).", r.error.c_str());
        ui->text_wrapped(ui->user, buf);
        if (ui->button(ui->user, "Try again", 0.0f)) net::router_open(port);
        break;
    }
}

void build_addresses(const SplatModUi* ui, uint16_t port, const net::RouterStatus& r) {
    const auto now = std::chrono::steady_clock::now();
    if (s_local_ip_checked == std::chrono::steady_clock::time_point::min() ||
        now - s_local_ip_checked > std::chrono::seconds(10)) {
        s_local_ip = net::local_ip();
        s_local_ip_checked = now;
    }
    const std::string suffix = ":" + std::to_string(port);
    const std::string looked_up = net::public_ip();
    const bool router_public = !r.external_ip.empty() && !net::is_private_ipv4(r.external_ip);
    const bool looked_public = !looked_up.empty() && !net::is_private_ipv4(looked_up);
    const std::string internet = router_public            ? r.external_ip
                                 : looked_public          ? looked_up
                                 : !r.external_ip.empty() ? r.external_ip
                                                          : looked_up;
    if (!internet.empty() && !net::is_private_ipv4(internet)) {
        address_row(ui, "Friends on the internet join:", internet + suffix, "internet");
    } else if (!internet.empty()) {
        char buf[512];
        std::snprintf(buf, sizeof(buf), "Your router's internet address %s is not reachable from the internet.",
                      internet.c_str());
        text_bad(ui, buf);
        ui->text_wrapped(ui->user, "Your router sits behind another router, or "
                                   "your provider shares one address "
                                   "between customers (carrier-grade NAT). Port "
                                   "forwarding cannot work then. Use a "
                                   "virtual LAN such as Tailscale, ZeroTier or "
                                   "Radmin VPN on both PCs and give your "
                                   "friend that address.");
    } else {
        ui->text(ui->user, "Friends on the internet join:");
        ui->same_line(ui->user);
        if (net::public_ip_busy()) {
            text_muted(ui, "looking it up ...");
        } else if (ui->button(ui->user, "Find my internet address", 0.0f)) {
            net::public_ip_lookup();
        }
        text_muted(ui, "(Asks api.ipify.org which public address your internet "
                       "connection has.)");
    }
    if (!r.external_ip.empty() && !looked_up.empty() && r.external_ip != looked_up) {
        char buf[512];
        std::snprintf(buf, sizeof(buf),
                      "Note: your router reports %s, but the internet sees %s. A "
                      "second router is in between; forward "
                      "the port on that one too.",
                      r.external_ip.c_str(), looked_up.c_str());
        ui->text_wrapped(ui->user, buf);
    }
    if (!s_local_ip.empty()) address_row(ui, "Same home network:", s_local_ip + suffix, "lan");
}

void build_manual_help(const SplatModUi* ui, uint16_t port, const net::RouterStatus& r) {
    if (!ui->button(ui->user,
                    s_show_manual_help ? "Hide manual port forwarding guide" : "How to forward the port by hand",
                    0.0f)) {
        // Check active toggle state below.
    } else {
        s_show_manual_help = !s_show_manual_help;
    }
    if (!s_show_manual_help) return;

    const std::string router = r.router.empty() ? "192.168.0.1 or 192.168.1.1" : r.router;
    const std::string pc = s_local_ip.empty() ? "this PC" : s_local_ip;
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "1. Open your router's page in a browser: %s (the address is "
                  "often printed on the router).",
                  router.c_str());
    ui->text_wrapped(ui->user, buf);
    if (!r.router.empty() && ui->button(ui->user, "Open router page", 0.0f)) net::open_url("http://" + r.router);
    ui->text_wrapped(ui->user, "2. Find \"Port forwarding\" (also called \"Virtual "
                               "server\", \"NAT\" or \"Port sharing\").");
    std::snprintf(buf, sizeof(buf), "3. Forward TCP port %u to %s (internal and external port %u).", unsigned(port),
                  pc.c_str(), unsigned(port));
    ui->text_wrapped(ui->user, buf);
    ui->text_wrapped(ui->user, "4. When Windows asks about the firewall, allow "
                               "Splatoon on private and public networks.");
    std::snprintf(buf, sizeof(buf),
                  "5. Send your friend your internet address with the port, for "
                  "example 203.0.113.7:%u.",
                  unsigned(port));
    ui->text_wrapped(ui->user, buf);
}

void build_host(const SplatModUi* ui, rs::Values& v, bool& dirty) {
    ui->separator_text(ui->user, "Host: a friend joins your game");
    const uint16_t port = net::host_port();
    if (!port) {
        if (!s_port_loaded) {
            std::snprintf(s_port_buf, sizeof(s_port_buf), "%d", v.net_port);
            s_port_loaded = true;
        }
        if (ui->input_text(ui->user, "Port##host_port", "52815", s_port_buf, sizeof(s_port_buf))) {
            char* end = nullptr;
            const long parsed = std::strtol(s_port_buf, &end, 10);
            if (end != s_port_buf && *end == '\0') {
                const int clamped = std::clamp(int(parsed), 1024, 65535);
                if (v.net_port != clamped) {
                    v.net_port = clamped;
                    dirty = true;
                }
            }
        }
        text_muted(ui, "TCP port your friend connects to (default 52815).");
        dirty |= ui->checkbox(ui->user, "Open the port on my router automatically (UPnP)", &v.net_upnp);
        if (ui->button(ui->user, "Start hosting", 0.0f)) {
            std::snprintf(s_port_buf, sizeof(s_port_buf), "%d", v.net_port);
            if (net::host_start(uint16_t(v.net_port))) {
                s_message.clear();
                if (v.net_upnp) net::router_open(uint16_t(v.net_port));
            } else {
                s_message = "Port " + std::to_string(v.net_port) + " is already in use. Choose another port.";
            }
        }
        text_muted(ui, "Your friend plays as Player 2 with a controller or "
                       "keyboard and mouse.");
        return;
    }

    char status_buf[128];
    if (net::host_guest_connected()) {
        text_good(ui, "Your friend is connected and plays as Player 2.");
    } else {
        std::snprintf(status_buf, sizeof(status_buf), "Waiting for your friend on port %u ...", unsigned(port));
        text_wait(ui, status_buf);
    }
    ui->same_line(ui->user);
    if (ui->button(ui->user, "Stop hosting", 0.0f)) {
        net::host_stop();
        net::router_close();
        return;
    }
    const net::RouterStatus r = net::router_status();
    router_line(ui, r, port);
    build_addresses(ui, port, r);
    build_manual_help(ui, port, r);
    text_muted(ui, "In Battle Dojo your friend sees the TV picture; Tab shows "
                   "you the GamePad screen.");
}

bool build_join(const SplatModUi* ui, rs::Values& v, bool& dirty) {
    ui->separator_text(ui->user, "Join: play in a friend's game");
    if (!s_join_loaded) {
        std::snprintf(s_join, sizeof(s_join), "%s", v.net_join.c_str());
        s_join_loaded = true;
    }
    ui->text_wrapped(ui->user, "Enter the address your friend sent you. Your game closes "
                               "and your friend's game opens in a window.");
    ui->input_text(ui->user, "Address##join", "address or address:port", s_join, sizeof(s_join));
    ui->same_line(ui->user);
    if (ui->button(ui->user, "Join", 0.0f)) {
        std::string clean;
        if (net::host_port()) {
            s_message = "Stop hosting first.";
        } else if (!net::clean_address(s_join, clean)) {
            s_message = "That is not an address. It looks like 203.0.113.7 or "
                        "203.0.113.7:52815.";
        } else {
            s_message.clear();
            std::snprintf(s_join, sizeof(s_join), "%s", clean.c_str());
            if (v.net_join != clean) {
                v.net_join = clean;
                dirty = true;
            }
            s_confirm_join = true;
        }
    }
    if (!s_confirm_join) return false;

    text_wait(ui, "Your game closes now. Progress since the last save is lost.");
    if (ui->button(ui->user, "Close my game and join", 0.0f)) {
        s_confirm_join = false;
        if (net::launch_guest(s_join)) return true;
        s_message = "The game window for joining could not be started.";
    }
    ui->same_line(ui->user);
    if (ui->button(ui->user, "Cancel##join", 0.0f)) s_confirm_join = false;
    return false;
}

} // namespace

int draw_page(const SplatModUi* ui, void* user) {
    if (!ui) return SPLATMOD_MENU_STAY;
    const auto* host = static_cast<const SplatModHost*>(user);

    auto v = rs::get();
    bool dirty = false;

    build_host(ui, v, dirty);
    const bool joined = build_join(ui, v, dirty);

    ui->separator_text(ui->user, "Story Mode co-op");
    dirty |= ui->checkbox(ui->user, "Allow Player 2 in Octo Valley levels", &v.coop_story);
    text_muted(ui, "Activates automatically on level entry when a second player "
                   "is connected.");

    if (ui->button(ui->user, s_show_stream ? "Hide Stream Quality" : "Stream Quality", 0.0f)) {
        s_show_stream = !s_show_stream;
    }
    if (s_show_stream) {
        dirty |= ui->slider_int(ui->user, "Video quality", &v.net_kbps, 2000, 30000, "%d kbit/s");
        text_muted(ui, "Picture quality sent to your friend; applies when they (re)connect.");
    }

    if (!s_message.empty()) text_bad(ui, s_message.c_str());
    if (dirty) rs::set(v);

    if (joined) {
        if (host && host->request_quit) {
            host->request_quit();
            return SPLATMOD_MENU_STAY;
        }
        return SPLATMOD_MENU_QUIT;
    }
    return SPLATMOD_MENU_STAY;
}

const char* status_line(void* /*user*/) {
    if (!net::host_port()) return "";
    return net::host_guest_connected() ? "friend connected" : "hosting";
}

} // namespace online_menu
