#include "coop_hooks.h"

#include "coop_views.h"
#include "mod_log.h"
#include "mod_settings.h"
#include "netplay.h"
#include "splat_mod_api.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <initializer_list>
#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

const SplatModHost* s_host = nullptr;
bool s_hooks_installed = false;

std::mutex s_players_mutex;
std::vector<uint32_t> s_players;

std::atomic<bool> s_coop{false};
std::atomic<bool> s_two_views{false};
std::atomic<bool> s_dojo_match{false};
std::atomic<uint32_t> s_frame_counter{0};

constexpr uint32_t kControllerBindingName = 0x0265A730u;
constexpr uint32_t kSceneCtor = 0x023FE9C8u;
constexpr uint32_t kPlayerSetup = 0x023FED60u;
constexpr uint32_t kIsDuel = 0x026BA97Cu;
constexpr uint32_t kIsDuel2 = 0x026BA904u;
constexpr uint32_t kCreate3DLayer = 0x0289F358u;
constexpr uint32_t kAttachLayer = 0x02A3F070u;
constexpr uint32_t kCtrlMgrPtr = 0x101DDF64u;
constexpr uint32_t kGetVpad = 0x028AAB78u;
constexpr uint32_t kGetKpad = 0x028AAB54u;
constexpr uint32_t kCtrlBind1P = 0x02037214u;
constexpr uint32_t kCtrlBind2P = 0x02037394u;
constexpr uint32_t kCtrlSetSettings = 0x0203717Cu;
constexpr uint32_t kDynResMgrPtr = 0x101E78B0u;
constexpr uint32_t kCubemapRender = 0x029E7B00u;
constexpr uint32_t kCubemapUpdate = 0x029E9608u;
constexpr uint32_t kSpawnSetup = 0x0240B07Cu;
constexpr uint32_t kBindControllers = 0x023FF448u;
constexpr uint32_t kReleasePlayer = 0x02652E84u;
constexpr uint32_t kMissionStartReturn = 0x025FEAA8u;
constexpr uint32_t kLayer3DCubemapInit = 0x02A96208u;
constexpr uint32_t kLytAddTarget = 0x02783D90u;
constexpr uint32_t kLytTvHud = 6;
constexpr uint32_t kLytSecondHud = 8;
constexpr uint32_t kLytTvOverlay = 18;
constexpr uint32_t kLytSecondOverlay = 20;
constexpr uint32_t kMsnDrcBlockTarget = 0x02488D28u;
constexpr uint32_t kMsnDrcCourseClear = 0x02488D8Cu;
constexpr uint32_t kLytTargetCount = 0x14u;
constexpr uint32_t kAlloc = 0x028CD9F0u;
constexpr uint32_t kMainTvCtor = 0x02489BECu;
constexpr uint32_t kMainTvSize = 0x244u;
constexpr uint32_t kAmiiboTimerCtor = 0x0248B5E8u;
constexpr uint32_t kAmiiboTimerReturn = 0x02488C20u;
constexpr uint32_t kHudFeed = 0x023FCBBCu;
constexpr uint32_t kLytFlags = 0x118u;
constexpr uint32_t kLytMask = 0x11Cu;
constexpr uint32_t kLytHidden = 0x2u;
constexpr uint32_t kLytState = 0x1E8u;
constexpr uint32_t kLytDetach = 0x028B80BCu;
constexpr uint32_t kMainTvStartAnims = 0x02489CC0u;

uint32_t s_tv_3d_layer = 0, s_drc_3d_layer = 0, s_tv_3d_object = 0;
bool s_injected_slot1 = false;
bool s_ctrl_bound = false;
uint32_t s_view_override = 0, s_view_saved = 0;

struct CubemapConfig {
    uint32_t ccc = 0, cdc = 0, w109c = 0, w10ac = 0, w10bc = 0, w10cc = 0;
    uint8_t b7b0 = 0, b108c = 0, b10dc = 0;
};
CubemapConfig s_tv_cubemap;
bool s_tv_cubemap_valid = false;
uint32_t s_main_tv2 = 0;
float s_p2_hud_last = 0.0f;
uint32_t s_dojo_hud = 0, s_label_layout = 0, s_p1_label = 0;

bool mem_ok(const uint8_t* mem, uint32_t addr, uint64_t size) {
    return s_host && s_host->memory_accessible && s_host->memory_accessible(mem, addr, size);
}

int scene_kind(uint8_t* mem) {
    if (!s_host || !s_host->active_scene_type) return -1;
    return s_host->active_scene_type(mem, nullptr);
}

std::string read_name(uint8_t* mem, uint32_t address) {
    std::string name;
    for (uint32_t i = 0; i < 96 && address && mem_ok(mem, address + i, 1); ++i) {
        const char c = char(splatmod_read8(mem, address + i));
        if (!c) break;
        name += c;
    }
    return name;
}

bool coop() {
    return s_coop.load(std::memory_order_relaxed);
}

bool coop_level(int kind) {
    return kind == 1 || kind == 3;
}

bool coop_scene(uint8_t* mem) {
    return coop() && coop_level(scene_kind(mem));
}

bool call_guest_with_frame(CPUState* cpu, uint32_t addr, std::initializer_list<uint32_t> args,
                           uint32_t* out_r3 = nullptr) {
    if (!cpu || !s_host || !s_host->call_guest) return false;
    const uint32_t sp = (cpu->r[1] - 0x40u) & ~0xFu;
    if (!mem_ok(cpu->mem, sp, 0x40u)) return false;

    const CPUState saved = *cpu;
    splatmod_write32(cpu->mem, sp, saved.r[1]);
    cpu->r[1] = sp;

    uint32_t arg_buf[8]{};
    uint32_t count = 0;
    for (uint32_t arg : args) {
        if (count < 8u) arg_buf[count++] = arg;
    }
    uint32_t ret = 0;
    const bool ok = s_host->call_guest(cpu, addr, arg_buf, count, &ret);
    *cpu = saved;
    if (ok && out_r3) *out_r3 = ret;
    return ok;
}

uint32_t find_player(uint8_t* mem, uint32_t index) {
    std::lock_guard lock(s_players_mutex);
    for (auto it = s_players.rbegin(); it != s_players.rend(); ++it) {
        if (mem_ok(mem, *it, 0xA0) && splatmod_read32(mem, *it + 0x9C) == index) return *it;
    }
    return 0;
}

void bind_coop_controllers(CPUState* cpu, bool two_players) {
    uint8_t* mem = cpu->mem;
    const uint32_t ctrl_mgr = splatmod_read32(mem, kCtrlMgrPtr);
    if (!ctrl_mgr || !mem_ok(mem, ctrl_mgr, 0x190u)) return;
    uint32_t vpad = 0;
    if (!call_guest_with_frame(cpu, kGetVpad, {}, &vpad) || !vpad) return;
    const uint32_t saved_r1 = cpu->r[1];
    const uint32_t scratch = (saved_r1 - 0x40u) & ~0xFu;
    if (!mem_ok(mem, scratch, 0x40u)) return;
    splatmod_write32(mem, scratch, saved_r1);
    cpu->r[1] = scratch;
    if (two_players) {
        uint32_t kpad0 = 0;
        if (call_guest_with_frame(cpu, kGetKpad, {0u}, &kpad0) && kpad0) {
            splatmod_write32(mem, scratch + 0x10u, 0u);
            splatmod_write32(mem, scratch + 0x14u, 3u);
            splatmod_write32(mem, scratch + 0x18u, splatmod_read32(mem, ctrl_mgr + 0x17Cu));
            splatmod_write32(mem, scratch + 0x1Cu, 0u);
            call_guest_with_frame(cpu, kCtrlSetSettings, {ctrl_mgr, 1u, scratch + 0x18u});
            call_guest_with_frame(cpu, kCtrlBind2P,
                                  {ctrl_mgr, vpad, scratch + 0x10u, vpad, kpad0, scratch + 0x14u, 0u});
            s_ctrl_bound = true;
        }
    } else if (s_ctrl_bound) {
        splatmod_write32(mem, scratch + 0x10u, 0u);
        call_guest_with_frame(cpu, kCtrlBind1P, {ctrl_mgr, vpad, scratch + 0x10u});
        s_ctrl_bound = false;
    }
    cpu->r[1] = saved_r1;
}

uint32_t display_object(uint8_t* mem, uint32_t index) {
    const uint32_t mgr = splatmod_read32(mem, 0x101E7698u);
    if (!mgr || !mem_ok(mem, mgr + 0xF38u, 8)) return 0;
    const uint32_t count = splatmod_read32(mem, mgr + 0xF38u);
    const uint32_t list = splatmod_read32(mem, mgr + 0xF3Cu);
    if (!list || index >= count || !mem_ok(mem, list + 4 * index, 4)) return 0;
    const uint32_t object = splatmod_read32(mem, list + 4 * index);
    return object && mem_ok(mem, object + 0x9DCu, 4) ? object : 0;
}

void force_second_view(uint8_t* mem) {
    const uint32_t second = display_object(mem, 7);
    if (!second || s_view_override) return;
    s_view_saved = splatmod_read32(mem, second + 0x9DCu);
    if (s_view_saved == 1) return;
    s_view_override = second;
    splatmod_write32(mem, second + 0x9DCu, 1);
}

void restore_second_view(uint8_t* mem) {
    if (!s_view_override) return;
    splatmod_write32(mem, s_view_override + 0x9DCu, s_view_saved);
    s_view_override = 0;
}

uint32_t player_slots(uint8_t* mem) {
    const uint32_t holder = splatmod_read32(mem, 0x101DD330u);
    if (!holder || !mem_ok(mem, holder + 0x10, 4)) return 0;
    const uint32_t slots = splatmod_read32(mem, holder + 0x10);
    return slots && mem_ok(mem, slots, 32) ? slots : 0;
}

bool cube_map_empty(CPUState* cpu) {
    if (!coop_scene(cpu->mem)) return false;
    uint8_t* mem = cpu->mem;
    const uint32_t pipeline = cpu->r[3];
    const uint32_t env = splatmod_read32(mem, pipeline + 0x170u);
    const uint32_t ctx = splatmod_read32(mem, pipeline + 0x140u);
    const uint32_t tex = ctx && mem_ok(mem, ctx + 4u, 4) ? splatmod_read32(mem, ctx + 4u) : 0;
    const bool env_ok = env && mem_ok(mem, env + 0x1F4u, 0x1Cu);
    const bool tex_ok = tex && mem_ok(mem, tex, 0x20u);
    const uint32_t env_size = env_ok ? splatmod_read32(mem, env + 0x1F4u) : 0;
    const uint32_t env_count = env_ok ? splatmod_read32(mem, env + 0x1FCu) : 0;
    const uint32_t env_format = env_ok ? splatmod_read32(mem, env + 0x208u) : 0;
    const uint32_t tex_width = tex_ok ? splatmod_read32(mem, tex + 0x08u) : 0;
    const uint32_t tex_format = tex_ok ? splatmod_read32(mem, tex + 0x18u) : 0;
    return !ctx || !env_size || !env_count || !env_format || env_format >= 0x3Bu || !tex_width || tex_width > 4096u ||
           !tex_format;
}

bool pane_named(uint8_t* mem, uint32_t pane, const char* name) {
    for (uint32_t i = 0; i < 24; ++i) {
        const char c = char(splatmod_read8(mem, pane + 0x80 + i));
        if (c != name[i]) return false;
        if (!c) return true;
    }
    return false;
}

uint32_t find_pane(uint8_t* mem, uint32_t pane, const char* name, int depth = 0) {
    if (!pane || depth > 16 || !mem_ok(mem, pane, 0xD8)) return 0;
    if (pane_named(mem, pane, name)) return pane;
    const uint32_t end = pane + 0x14;
    uint32_t guard = 0;
    for (uint32_t child = splatmod_read32(mem, end); child && child != end && guard < 256;
         child = splatmod_read32(mem, child), ++guard) {
        if (const uint32_t found = find_pane(mem, child, name, depth + 1)) return found;
    }
    return 0;
}

void set_pane_xy(uint8_t* mem, uint32_t pane, float x, float y) {
    splatmod_write_f32(mem, pane + 0x1C, x);
    splatmod_write_f32(mem, pane + 0x20, y);
    splatmod_write8(mem, pane + 0x44, uint8_t(splatmod_read8(mem, pane + 0x44) | 0x10));
}

void set_pane_visible(uint8_t* mem, uint32_t pane, bool visible) {
    splatmod_write8(mem, pane + 0x44, uint8_t((splatmod_read8(mem, pane + 0x44) & ~1u) | (visible ? 1u : 0u)));
}

void set_player_label(uint8_t* mem, uint32_t text_box, char digit) {
    const uint32_t buffer = splatmod_read32(mem, text_box + 0xA4);
    if (!buffer || splatmod_read16(mem, text_box + 0xCC) < 3 || !mem_ok(mem, buffer, 6)) return;
    if (splatmod_read16(mem, buffer) == 'P' && splatmod_read16(mem, buffer + 2) == uint16_t(digit) &&
        splatmod_read16(mem, text_box + 0xCE) == 2)
        return;
    splatmod_write16(mem, buffer, 'P');
    splatmod_write16(mem, buffer + 2, uint16_t(digit));
    splatmod_write16(mem, buffer + 4, 0);
    splatmod_write16(mem, text_box + 0xCE, 2);
    splatmod_write8(mem, text_box + 0xD0, uint8_t(splatmod_read8(mem, text_box + 0xD0) | 0x20));
}

void dojo_player_labels(CPUState* cpu, bool build) {
    uint8_t* mem = cpu->mem;
    if (!s_dojo_hud || !rbrew_net::host_guest_connected() || !mem_ok(mem, s_dojo_hud + 0x38, 4)) return;
    const uint32_t hud = splatmod_read32(mem, s_dojo_hud + 0x38);
    const uint32_t layout = hud && mem_ok(mem, hud + 0x30, 4) ? splatmod_read32(mem, hud + 0x30) : 0;
    const uint32_t root = layout && mem_ok(mem, layout + 0x1C, 4) ? splatmod_read32(mem, layout + 0x1C) : 0;
    if (!root) return;
    if (hud != s_label_layout) {
        s_label_layout = hud;
        s_p1_label = 0;
    }
    const uint32_t box1 = find_pane(mem, root, "N_DRC_00");
    const uint32_t box2 = find_pane(mem, root, "N_TV_00");
    const uint32_t icon1 = find_pane(mem, root, "P_DRC_00");
    const uint32_t icon2 = find_pane(mem, root, "P_TV_00");
    const uint32_t text2 = find_pane(mem, root, "T_TV_00");
    if (!box1 || !box2 || !icon1 || !icon2 || !text2 || !splatmod_read32(mem, text2 + 0xB4)) return;
    if (!s_p1_label) {
        if (!build) return;
        uint32_t clone = 0;
        call_guest_with_frame(cpu, 0x02104C28u, {0u, text2, box1}, &clone);
        if (!clone) return;
        set_pane_xy(mem, clone, -46.0f, 6.0f);
        splatmod_write8(mem, clone + 0x80, 'X');
        s_p1_label = clone;
    }
    if (splatmod_read32(mem, text2 + 0x0C) == icon2) {
        if (!build) return;
        call_guest_with_frame(cpu, 0x0292BC08u, {icon2, text2});
        call_guest_with_frame(cpu, 0x0292BBB0u, {box2, text2});
        set_pane_xy(mem, text2, 46.0f, 6.0f);
    }
    set_pane_visible(mem, icon1, false);
    set_pane_visible(mem, icon2, false);
    set_player_label(mem, s_p1_label, '1');
    set_player_label(mem, text2, '2');
}

void create_second_main_tv(CPUState* cpu, uint32_t block) {
    uint8_t* mem = cpu->mem;
    uint32_t obj = 0;
    if (!call_guest_with_frame(cpu, kAlloc, {kMainTvSize}, &obj) || !obj || !mem_ok(mem, obj, kMainTvSize)) return;
    for (uint32_t i = 0; i < kMainTvSize; i += 4) splatmod_write32(mem, obj + i, 0);
    call_guest_with_frame(cpu, kMainTvCtor, {obj});
    const uint32_t vtable = splatmod_read32(mem, obj);
    const uint32_t init = vtable && mem_ok(mem, vtable + 0x34, 4) ? splatmod_read32(mem, vtable + 0x34) : 0;
    if (init && call_guest_with_frame(cpu, init, {obj, block})) s_main_tv2 = obj;
}

void mirror_level_hud(CPUState* cpu, uint32_t handler, uint32_t tv1, uint32_t tv2, bool both_shown) {
    uint8_t* mem = cpu->mem;
    constexpr uint32_t kSetScrollAnimated = 0x0248A030u;
    constexpr uint32_t kSetScrollStatic = 0x024893C4u;
    constexpr uint32_t kSetKeys = 0x0248A0F4u;
    constexpr uint32_t kSetEggs = 0x024C39A0u;
    constexpr uint32_t kSetLivesTarget = 0x024C38DCu;
    constexpr uint32_t kSetLivesStatic = 0x024C3784u;
    constexpr uint32_t kPaneAnimClamp = 0x0248A268u;
    constexpr uint32_t kPaneAnimStop = 0x0248A280u;
    const uint8_t scroll = splatmod_read8(mem, tv1 + 0x225);
    if (splatmod_read8(mem, tv2 + 0x225) != scroll) {
        if (scroll && both_shown) {
            call_guest_with_frame(cpu, kSetScrollAnimated, {tv2, scroll});
        } else {
            const uint32_t saved = splatmod_read32(mem, handler + 0x3C);
            splatmod_write32(mem, handler + 0x3C, tv2);
            call_guest_with_frame(cpu, kSetScrollStatic, {handler, scroll});
            splatmod_write32(mem, handler + 0x3C, saved);
        }
    }
    const uint32_t keys = splatmod_read32(mem, tv1 + 0x234);
    if (splatmod_read32(mem, tv2 + 0x234) != keys) call_guest_with_frame(cpu, kSetKeys, {tv2, keys});
    const uint32_t info1 = splatmod_read32(mem, tv1 + 0x21C);
    const uint32_t info2 = splatmod_read32(mem, tv2 + 0x21C);
    if (info1 && info2 && mem_ok(mem, info1, 0x48) && mem_ok(mem, info2, 0x48)) {
        const uint32_t flags = splatmod_read32(mem, info2 + 0x10);
        const uint32_t eggs = splatmod_read32(mem, info1 + 0x44);
        if ((flags & 0x10u) && splatmod_read32(mem, info2 + 0x44) != eggs)
            call_guest_with_frame(cpu, kSetEggs, {info2, eggs});
        if (flags & 0x4u) {
            const uint32_t shown = splatmod_read32(mem, info1 + 0x34);
            const uint32_t target = splatmod_read32(mem, info1 + 0x38);
            if (!both_shown && splatmod_read32(mem, info2 + 0x34) != shown)
                call_guest_with_frame(cpu, kSetLivesStatic, {info2, shown});
            if (splatmod_read32(mem, info2 + 0x38) != target)
                call_guest_with_frame(cpu, kSetLivesTarget, {info2, target});
        }
    }
    const uint32_t anim1 = splatmod_read32(mem, tv1 + 0x240);
    const uint32_t anim2 = splatmod_read32(mem, tv2 + 0x240);
    if (anim1 == 0 && anim2 != 0)
        call_guest_with_frame(cpu, kPaneAnimStop, {tv2});
    else if (anim1 == 1 && anim2 > 1)
        call_guest_with_frame(cpu, kPaneAnimClamp, {tv2});
}

void mirror_hud_state(CPUState* cpu, uint32_t handler, uint32_t tv1) {
    uint8_t* mem = cpu->mem;
    const uint32_t tv2 = s_main_tv2;
    const auto ok = [&](uint32_t o) { return o && mem_ok(mem, o, kMainTvSize); };
    const auto hidden = [&](uint32_t o) { return (splatmod_read32(mem, o + kLytFlags) & kLytHidden) != 0; };
    const auto state = [&](uint32_t o) { return splatmod_read8(mem, o + kLytState); };
    const auto vcall = [&](uint32_t o, uint32_t off) {
        const uint32_t vt = splatmod_read32(mem, o);
        if (vt && mem_ok(mem, vt + off, 4)) call_guest_with_frame(cpu, splatmod_read32(mem, vt + off), {o});
    };
    if (ok(tv1) && ok(tv2)) {
        const uint8_t s1 = state(tv1);
        const uint8_t s2 = state(tv2);
        if (!hidden(tv1) && hidden(tv2) && s1 >= 1 && s1 <= 3) {
            vcall(tv2, 0x164);
            if (splatmod_read32(mem, tv1 + 0x240) != 0) {
                const uint32_t info = splatmod_read32(mem, 0x101DD330u);
                if (info) call_guest_with_frame(cpu, kMainTvStartAnims, {tv2, info + 0x1C});
            }
        } else if (s1 == 4 && !hidden(tv2) && s2 != 4 && s2 != 0) {
            vcall(tv2, 0x16C);
        } else if (hidden(tv1) && !hidden(tv2)) {
            vcall(tv2, 0x94);
        }
        mirror_level_hud(cpu, handler, tv1, tv2, !hidden(tv1) && !hidden(tv2));
    }
    const uint32_t second = splatmod_read32(mem, handler + 0x2C);
    const uint32_t drc = splatmod_read32(mem, handler + 0x40);
    if (second < 32 && ok(drc) && (splatmod_read32(mem, drc + kLytMask) & (1u << second)))
        call_guest_with_frame(cpu, kLytDetach, {drc, second, 1u});
}

void place_player2(uint8_t* mem) {
    const uint32_t first = find_player(mem, 0);
    const uint32_t second = find_player(mem, 1);
    if (!first || !second || !mem_ok(mem, first, 0x740) || !mem_ok(mem, second, 0x740)) return;
    if (splatmod_read_f32(mem, second + 0x248) != 0.0f || splatmod_read_f32(mem, second + 0x250) != 0.0f) return;
    const float x = splatmod_read_f32(mem, first + 0x248);
    const float y = splatmod_read_f32(mem, first + 0x24C);
    const float z = splatmod_read_f32(mem, first + 0x250);
    if (x == 0.0f && z == 0.0f) return;
    for (uint32_t base : {0x248u, 0x72Cu}) {
        splatmod_write_f32(mem, second + base, x + 15.0f);
        splatmod_write_f32(mem, second + base + 4, y + 10.0f);
        splatmod_write_f32(mem, second + base + 8, z);
    }
}

// ---- 16 SplatMod guest hook declarations ----

SPLAT_DEFINE_GUEST_HOOK(HookControllerBindingName)
SPLAT_DEFINE_GUEST_HOOK(HookSceneCtor)
SPLAT_DEFINE_GUEST_HOOK(HookPlayerSetup)
SPLAT_DEFINE_GUEST_HOOK(HookIsDuel)
SPLAT_DEFINE_GUEST_HOOK(HookIsDuel2)
SPLAT_DEFINE_GUEST_HOOK(HookCreate3DLayer)
SPLAT_DEFINE_GUEST_HOOK(HookAttachLayer)
SPLAT_DEFINE_GUEST_HOOK(HookCubemapRender)
SPLAT_DEFINE_GUEST_HOOK(HookCubemapUpdate)
SPLAT_DEFINE_GUEST_HOOK(HookSpawnSetup)
SPLAT_DEFINE_GUEST_HOOK(HookBindControllers)
SPLAT_DEFINE_GUEST_HOOK(HookReleasePlayer)
SPLAT_DEFINE_GUEST_HOOK(HookLayer3DCubemapInit)
SPLAT_DEFINE_GUEST_HOOK(HookLytAddTarget)
SPLAT_DEFINE_GUEST_HOOK(HookAmiiboTimerCtor)
SPLAT_DEFINE_GUEST_HOOK(HookHudFeed)

void HookControllerBindingName::Callback(CPUState* cpu, uint32_t /*guest_addr*/) {
    const uint32_t player = cpu->r[3];
    Orig(cpu);
    std::lock_guard lock(s_players_mutex);
    if (std::find(s_players.begin(), s_players.end(), player) == s_players.end() && s_players.size() < 16)
        s_players.push_back(player);
}

void HookSceneCtor::Callback(CPUState* cpu, uint32_t /*guest_addr*/) {
    s_tv_3d_layer = s_drc_3d_layer = s_tv_3d_object = 0;
    s_tv_cubemap_valid = false;
    s_main_tv2 = 0;
    s_p2_hud_last = 0.0f;
    const int kind = scene_kind(cpu->mem);
    const uint32_t views = cpu->r[4];
    const uint32_t cams = cpu->r[5];
    const uint32_t tag = cpu->r[6];
    const bool story = kind == 1 && views == 1 && cams == 1 && tag == 0x11u;
    const bool hub = kind == 3 && views == 1 && cams == 1;
    if (!story && !hub && s_ctrl_bound && kind != 6 && kind != 7) bind_coop_controllers(cpu, false);
    const bool two = (story || hub) && rbrew_mod_settings::g_coop_story.load(std::memory_order_relaxed) &&
                     rbrew_coop::second_player_present();
    s_coop.store(two, std::memory_order_relaxed);
    if (two) {
        cpu->r[4] = 2;
        cpu->r[5] = 2;
    }
    Orig(cpu);
}

void HookBindControllers::Callback(CPUState* cpu, uint32_t /*guest_addr*/) {
    if (coop_scene(cpu->mem)) {
        bind_coop_controllers(cpu, true);
        const uint32_t dyn = splatmod_read32(cpu->mem, kDynResMgrPtr);
        if (dyn && mem_ok(cpu->mem, dyn + 0x48u, 4))
            splatmod_write32(cpu->mem, dyn + 0x48u, splatmod_read32(cpu->mem, dyn + 0x48u) & ~1u);
        force_second_view(cpu->mem);
    }
    Orig(cpu);
    restore_second_view(cpu->mem);
}

void HookReleasePlayer::Callback(CPUState* cpu, uint32_t /*guest_addr*/) {
    const CPUState entry = *cpu;
    Orig(cpu);
    if (entry.lr != kMissionStartReturn || !coop() || scene_kind(cpu->mem) != 1) return;
    const uint32_t second = find_player(cpu->mem, 1);
    if (!second || second == entry.r[3]) return;
    const CPUState after = *cpu;
    *cpu = entry;
    cpu->r[3] = second;
    Orig(cpu);
    *cpu = after;
}

void HookPlayerSetup::Callback(CPUState* cpu, uint32_t /*guest_addr*/) {
    {
        std::lock_guard lock(s_players_mutex);
        s_players.clear();
    }
    const uint32_t self = cpu->r[3];
    const int kind = scene_kind(cpu->mem);
    const bool two = coop() && coop_level(kind);
    if (two && mem_ok(cpu->mem, self + 0x2C8, 4) && splatmod_read32(cpu->mem, self + 0x2C8) == 1)
        splatmod_write32(cpu->mem, self + 0x2C8, 2);
    if (two) force_second_view(cpu->mem);

    const uint32_t slots = player_slots(cpu->mem);
    const uint32_t first = slots ? splatmod_read32(cpu->mem, slots) : 0;
    const uint32_t second = slots ? splatmod_read32(cpu->mem, slots + 4) : 0;
    constexpr uint32_t kSlotSize = 0xFC;
    const bool slots_ok = first && second && mem_ok(cpu->mem, first, kSlotSize) && mem_ok(cpu->mem, second, kSlotSize);
    if (two) {
        if (slots_ok && !splatmod_read8(cpu->mem, second + 4)) {
            for (uint32_t i = 4; i < kSlotSize; ++i)
                splatmod_write8(cpu->mem, second + i, splatmod_read8(cpu->mem, first + i));
            splatmod_write32(cpu->mem, second, 1);
            s_injected_slot1 = true;
        }
    } else if (s_injected_slot1) {
        if (kind != 6 && slots_ok) splatmod_write8(cpu->mem, second + 4, 0);
        s_injected_slot1 = false;
    }
    Orig(cpu);
}

void HookCreate3DLayer::Callback(CPUState* cpu, uint32_t /*guest_addr*/) {
    const uint32_t args = cpu->r[5];
    const uint32_t display = cpu->r[6];
    Orig(cpu);
    const std::string name = args ? read_name(cpu->mem, splatmod_read32(cpu->mem, args)) : std::string();
    if (name == "3D(TV)" && display == 0) s_tv_3d_layer = cpu->r[3];
    if (name == "Map3D(DRC)" && display == 1) s_drc_3d_layer = cpu->r[3];
}

void HookAttachLayer::Callback(CPUState* cpu, uint32_t /*guest_addr*/) {
    const uint32_t layer = cpu->r[3];
    if (layer && layer == s_tv_3d_layer) s_tv_3d_object = cpu->r[4];
    if (layer && layer == s_drc_3d_layer && s_tv_3d_object && coop_scene(cpu->mem)) cpu->r[4] = s_tv_3d_object;
    Orig(cpu);
}

void HookCubemapRender::Callback(CPUState* cpu, uint32_t /*guest_addr*/) {
    if (cube_map_empty(cpu)) {
        cpu->r[3] = 0;
        return;
    }
    Orig(cpu);
}

void HookCubemapUpdate::Callback(CPUState* cpu, uint32_t /*guest_addr*/) {
    if (cube_map_empty(cpu)) {
        const uint32_t pipeline = cpu->r[3];
        const uint32_t ctx = splatmod_read32(cpu->mem, pipeline + 0x140u);
        if (ctx && mem_ok(cpu->mem, ctx + 0x1560u, 4)) {
            const uint32_t remaining = splatmod_read32(cpu->mem, ctx + 0x1560u);
            if (remaining > 0) splatmod_write32(cpu->mem, ctx + 0x1560u, remaining - 1);
        }
        splatmod_write8(cpu->mem, pipeline + 0x16Du, 1);
        cpu->r[3] = cpu->r[4];
        return;
    }
    Orig(cpu);
}

void HookSpawnSetup::Callback(CPUState* cpu, uint32_t /*guest_addr*/) {
    const uint32_t self = cpu->r[3];
    Orig(cpu);
    if (!coop_scene(cpu->mem) || !self || !mem_ok(cpu->mem, self, 0x394u)) return;
    if (!splatmod_read8(cpu->mem, self + 0x384u)) return;
    const uint32_t src = self + 0x02Cu;
    const float offset = scene_kind(cpu->mem) == 3 ? 25.0f : 5.0f;
    struct Target {
        uint32_t mtx, valid;
    };
    const Target targets[] = {
        {self + 0x05Cu, self + 0x385u},
        {self + 0x1ACu, self + 0x38Cu},
        {self + 0x1DCu, self + 0x38Du},
    };
    for (const Target& t : targets) {
        if (splatmod_read8(cpu->mem, t.valid)) continue;
        for (uint32_t off = 0; off < 0x30u; off += 4)
            splatmod_write32(cpu->mem, t.mtx + off, splatmod_read32(cpu->mem, src + off));
        splatmod_write_f32(cpu->mem, t.mtx + 0x24u, splatmod_read_f32(cpu->mem, src + 0x24u) + offset);
        splatmod_write8(cpu->mem, t.valid, 1);
    }
}

void HookIsDuel::Callback(CPUState* cpu, uint32_t /*guest_addr*/) {
    if (coop_scene(cpu->mem)) {
        cpu->r[3] = 1;
        return;
    }
    Orig(cpu);
}

void HookIsDuel2::Callback(CPUState* cpu, uint32_t /*guest_addr*/) {
    if (coop_scene(cpu->mem)) {
        cpu->r[3] = 1;
    } else {
        Orig(cpu);
    }
    static uint32_t labelled_frame = ~0u;
    const uint32_t frame = s_frame_counter.load(std::memory_order_relaxed);
    if (frame != labelled_frame && scene_kind(cpu->mem) == 6) {
        labelled_frame = frame;
        dojo_player_labels(cpu, false);
    }
}

void HookLayer3DCubemapInit::Callback(CPUState* cpu, uint32_t /*guest_addr*/) {
    uint8_t* mem = cpu->mem;
    const uint32_t layer3d = cpu->r[3];
    const uint32_t cfg = layer3d && mem_ok(mem, layer3d + 4u, 4) ? splatmod_read32(mem, layer3d + 4u) : 0;
    if (!cfg || !mem_ok(mem, cfg, 0x10E0u)) {
        Orig(cpu);
        return;
    }
    const CubemapConfig current{
        splatmod_read32(mem, cfg + 0xCCCu),  splatmod_read32(mem, cfg + 0xCDCu),  splatmod_read32(mem, cfg + 0x109Cu),
        splatmod_read32(mem, cfg + 0x10ACu), splatmod_read32(mem, cfg + 0x10BCu), splatmod_read32(mem, cfg + 0x10CCu),
        splatmod_read8(mem, cfg + 0x7B0u),   splatmod_read8(mem, cfg + 0x108Cu),  splatmod_read8(mem, cfg + 0x10DCu)};
    if (current.ccc && current.cdc) {
        s_tv_cubemap = current;
        s_tv_cubemap_valid = true;
    } else if (s_tv_cubemap_valid && coop_scene(mem)) {
        const CubemapConfig& v = s_tv_cubemap;
        splatmod_write32(mem, cfg + 0xCCCu, v.ccc);
        splatmod_write32(mem, cfg + 0xCDCu, v.cdc);
        splatmod_write32(mem, cfg + 0x109Cu, v.w109c);
        splatmod_write32(mem, cfg + 0x10ACu, v.w10ac);
        splatmod_write32(mem, cfg + 0x10BCu, v.w10bc);
        splatmod_write32(mem, cfg + 0x10CCu, v.w10cc);
        splatmod_write8(mem, cfg + 0x7B0u, v.b7b0);
        splatmod_write8(mem, cfg + 0x108Cu, v.b108c);
        splatmod_write8(mem, cfg + 0x10DCu, v.b10dc);
    }
    Orig(cpu);
}

void HookAmiiboTimerCtor::Callback(CPUState* cpu, uint32_t /*guest_addr*/) {
    if (cpu->lr == kAmiiboTimerReturn && coop() && scene_kind(cpu->mem) == 1) {
        const uint32_t handler = cpu->r[31];
        call_guest_with_frame(cpu, kLytAddTarget, {cpu->r[1] + 8, splatmod_read32(cpu->mem, handler + 0x2C), 1u});
    }
    Orig(cpu);
}

void HookHudFeed::Callback(CPUState* cpu, uint32_t /*guest_addr*/) {
    const CPUState entry = *cpu;
    Orig(cpu);
    if (!s_main_tv2 || !coop() || scene_kind(cpu->mem) != 1) return;
    uint8_t* mem = cpu->mem;
    const uint32_t scene = entry.r[3];
    const uint32_t handler = splatmod_read32(mem, scene + 0x238);
    const uint32_t mgr_holder = splatmod_read32(mem, 0x101E2814u);
    const uint32_t mgr = mgr_holder ? splatmod_read32(mem, mgr_holder + 0x268) : 0;
    if (!handler || !mgr || !mem_ok(mem, handler + 0x3C, 0x24) || !mem_ok(mem, mgr + 0x2B0, 8)) return;
    const CPUState after = *cpu;
    const uint32_t main_tv = splatmod_read32(mem, handler + 0x3C);
    const uint32_t view0 = splatmod_read32(mem, mgr + 0x2B0);
    const double last = splatmod_read_f32(mem, scene + 0x244);
    const auto bound = [&](uint32_t slot) {
        const uint32_t camera = mem_ok(mem, scene + slot, 4) ? splatmod_read32(mem, scene + slot) : 0;
        return camera && mem_ok(mem, camera + 8, 4) ? splatmod_read32(mem, camera + 8) : 0u;
    };
    const uint32_t pad = splatmod_read32(mem, handler + 0x5C);
    const uint32_t pad1 = bound(0x280);
    const uint32_t pad2 = bound(0x284);
    const bool swap_pad = pad && pad == pad1 && pad2 && pad2 != pad1;
    splatmod_write32(mem, handler + 0x3C, s_main_tv2);
    splatmod_write32(mem, mgr + 0x2B0, splatmod_read32(mem, mgr + 0x2B4));
    splatmod_write_f32(mem, scene + 0x244, s_p2_hud_last);
    if (swap_pad) splatmod_write32(mem, handler + 0x5C, pad2);
    *cpu = entry;
    Orig(cpu);
    if (swap_pad) splatmod_write32(mem, handler + 0x5C, pad);
    s_p2_hud_last = float(splatmod_read_f32(mem, scene + 0x244));
    splatmod_write_f32(mem, scene + 0x244, float(last));
    splatmod_write32(mem, mgr + 0x2B0, view0);
    splatmod_write32(mem, handler + 0x3C, main_tv);
    mirror_hud_state(cpu, handler, main_tv);
    *cpu = after;
}

void HookLytAddTarget::Callback(CPUState* cpu, uint32_t /*guest_addr*/) {
    const int kind = scene_kind(cpu->mem);
    if (kind == 6) {
        if (cpu->lr == 0x024827B4u) {
            s_dojo_hud = cpu->r[27];
            s_label_layout = s_p1_label = 0;
        } else if (s_dojo_hud && !s_p1_label) {
            const CPUState saved = *cpu;
            dojo_player_labels(cpu, true);
            *cpu = saved;
        }
    }
    if (!coop() || !coop_level(kind)) {
        Orig(cpu);
        return;
    }
    if (kind == 3) {
        const uint32_t target = cpu->r[4];
        if (target == kLytSecondHud || target == kLytSecondOverlay) return;
        const CPUState entry = *cpu;
        Orig(cpu);
        if (target == kLytTvHud || target == kLytTvOverlay) {
            const CPUState after = *cpu;
            *cpu = entry;
            cpu->r[4] = target == kLytTvHud ? kLytSecondHud : kLytSecondOverlay;
            Orig(cpu);
            *cpu = after;
        }
        return;
    }
    if (cpu->lr == kMsnDrcBlockTarget) {
        const uint32_t params = cpu->r[3];
        Orig(cpu);
        create_second_main_tv(cpu, params);
        splatmod_write32(cpu->mem, params + kLytTargetCount, 0);
        return;
    }
    if (cpu->lr == kMsnDrcCourseClear) return;
    Orig(cpu);
}

#if defined(_WIN32)
bool local_xinput_p2_joined() {
    struct ModXInputGamepad {
        WORD wButtons;
        BYTE bLeftTrigger;
        BYTE bRightTrigger;
        SHORT sThumbLX;
        SHORT sThumbLY;
        SHORT sThumbRX;
        SHORT sThumbRY;
    };
    struct ModXInputState {
        DWORD dwPacketNumber;
        ModXInputGamepad Gamepad;
    };
    using XInputGetStateFn = DWORD(WINAPI*)(DWORD, ModXInputState*);

    static XInputGetStateFn s_get_state = []() -> XInputGetStateFn {
        HMODULE mod = LoadLibraryW(L"xinput1_4.dll");
        if (!mod) mod = LoadLibraryW(L"xinput9_1_0.dll");
        return mod ? reinterpret_cast<XInputGetStateFn>(GetProcAddress(mod, "XInputGetState")) : nullptr;
    }();
    static std::atomic<bool> s_joined{false};

    if (!s_get_state) return false;
    ModXInputState st{};
    if (s_get_state(1u, &st) != 0u) {
        s_joined.store(false, std::memory_order_relaxed);
        return false;
    }
    if (s_joined.load(std::memory_order_relaxed)) return true;
    if (st.Gamepad.bLeftTrigger > 128 && st.Gamepad.bRightTrigger > 128) {
        s_joined.store(true, std::memory_order_relaxed);
        return true;
    }
    return false;
}
#endif

} // namespace

namespace coop_hooks {

bool install(const SplatModHost* host) {
    s_host = host;
    if (!host || !host->hook_guest) return false;
    if (host->game_revision != 288u) {
        MOD_LOG_INFO("coop",
                     "Co-op story/Dojo guest hooks skipped (expected game revision "
                     "288, got %u)\n",
                     host->game_revision);
        return true;
    }

    auto install_one = [&](bool ok, uint32_t addr) {
        if (!ok) MOD_LOG_ERROR("coop", "Failed to install guest hook at 0x%08X\n", addr);
        return ok;
    };

    bool ok = true;
    ok = install_one(HookControllerBindingName::InstallAt(host, kControllerBindingName), kControllerBindingName) && ok;
    ok = install_one(HookSceneCtor::InstallAt(host, kSceneCtor), kSceneCtor) && ok;
    ok = install_one(HookPlayerSetup::InstallAt(host, kPlayerSetup), kPlayerSetup) && ok;
    ok = install_one(HookIsDuel::InstallAt(host, kIsDuel), kIsDuel) && ok;
    ok = install_one(HookIsDuel2::InstallAt(host, kIsDuel2), kIsDuel2) && ok;
    ok = install_one(HookCreate3DLayer::InstallAt(host, kCreate3DLayer), kCreate3DLayer) && ok;
    ok = install_one(HookAttachLayer::InstallAt(host, kAttachLayer), kAttachLayer) && ok;
    ok = install_one(HookCubemapRender::InstallAt(host, kCubemapRender), kCubemapRender) && ok;
    ok = install_one(HookCubemapUpdate::InstallAt(host, kCubemapUpdate), kCubemapUpdate) && ok;
    ok = install_one(HookSpawnSetup::InstallAt(host, kSpawnSetup), kSpawnSetup) && ok;
    ok = install_one(HookBindControllers::InstallAt(host, kBindControllers), kBindControllers) && ok;
    ok = install_one(HookReleasePlayer::InstallAt(host, kReleasePlayer), kReleasePlayer) && ok;
    ok = install_one(HookLayer3DCubemapInit::InstallAt(host, kLayer3DCubemapInit), kLayer3DCubemapInit) && ok;
    ok = install_one(HookLytAddTarget::InstallAt(host, kLytAddTarget), kLytAddTarget) && ok;
    ok = install_one(HookAmiiboTimerCtor::InstallAt(host, kAmiiboTimerCtor), kAmiiboTimerCtor) && ok;
    ok = install_one(HookHudFeed::InstallAt(host, kHudFeed), kHudFeed) && ok;

    s_hooks_installed = ok;
    if (!ok) uninstall(host);
    return ok;
}

void uninstall(const SplatModHost* host) {
    if (!host) host = s_host;
    if (host) {
        HookHudFeed::Uninstall(host);
        HookAmiiboTimerCtor::Uninstall(host);
        HookLytAddTarget::Uninstall(host);
        HookLayer3DCubemapInit::Uninstall(host);
        HookReleasePlayer::Uninstall(host);
        HookBindControllers::Uninstall(host);
        HookSpawnSetup::Uninstall(host);
        HookCubemapUpdate::Uninstall(host);
        HookCubemapRender::Uninstall(host);
        HookAttachLayer::Uninstall(host);
        HookCreate3DLayer::Uninstall(host);
        HookIsDuel2::Uninstall(host);
        HookIsDuel::Uninstall(host);
        HookPlayerSetup::Uninstall(host);
        HookSceneCtor::Uninstall(host);
        HookControllerBindingName::Uninstall(host);
    }

    s_hooks_installed = false;
    s_coop.store(false, std::memory_order_relaxed);
    s_two_views.store(false, std::memory_order_relaxed);
    s_dojo_match.store(false, std::memory_order_relaxed);
    {
        std::lock_guard lock(s_players_mutex);
        s_players.clear();
    }
    s_tv_3d_layer = s_drc_3d_layer = s_tv_3d_object = 0;
    s_injected_slot1 = false;
    s_ctrl_bound = false;
    s_view_override = s_view_saved = 0;
    s_tv_cubemap_valid = false;
    s_main_tv2 = 0;
    s_p2_hud_last = 0.0f;
    s_dojo_hud = s_label_layout = s_p1_label = 0;
    s_host = nullptr;
}

} // namespace coop_hooks

namespace rbrew_coop {

void bump_frame() {
    s_frame_counter.fetch_add(1u, std::memory_order_relaxed);
}

uint32_t current_frame() {
    return s_frame_counter.load(std::memory_order_relaxed);
}

void update_views(uint8_t* mem) {
    second_player_present();
    if (!s_host || s_host->game_revision != 288u) {
        (void)mem;
        return;
    }
    restore_second_view(mem);
    const int kind = scene_kind(mem);
    if (kind >= 0 && !coop_level(kind)) s_coop.store(false, std::memory_order_relaxed);
    const bool two = coop_scene(mem);
    s_two_views.store(two, std::memory_order_relaxed);
    s_dojo_match.store(kind == 6, std::memory_order_relaxed);
    if (kind == 6 && s_tv_3d_object && mem_ok(mem, s_tv_3d_object + 0x36F4, 12) &&
        splatmod_read32(mem, s_tv_3d_object + 0x36F4) >= 2 && splatmod_read8(mem, s_tv_3d_object + 0x36FD) > 1) {
        splatmod_write8(mem, s_tv_3d_object + 0x36FD, 1);
    }
    if (two) {
        const uint32_t dyn = splatmod_read32(mem, kDynResMgrPtr);
        if (dyn && mem_ok(mem, dyn + 0x48u, 4))
            splatmod_write32(mem, dyn + 0x48u, splatmod_read32(mem, dyn + 0x48u) & ~1u);
        place_player2(mem);
    }
}

bool two_views() {
    return s_two_views.load(std::memory_order_relaxed);
}

bool dojo_match() {
    return s_dojo_match.load(std::memory_order_relaxed);
}

bool second_player_present() {
    if (rbrew_net::host_guest_connected()) return true;
#if defined(_WIN32)
    return local_xinput_p2_joined();
#else
    return false;
#endif
}

} // namespace rbrew_coop
