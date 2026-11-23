# SplatPC Online Co-op Mod

Standalone native C++ mod for **SplatPC** adding:

- **Online Co-op Streaming**: Host your game and stream H.264 video + stereo audio to a friend's PC (`Splatoon.exe --join <ip>:<port>`), while their keyboard/mouse or controller drives Player 2's Wii U Pro Controller (`KPAD`).
- **Two-Player Story Mode Co-op (v288)**: Adds a second Inkling, second-screen 3D view/post-processing, mirrored HUD, and spawn placement across Octo Valley & single-player missions.
- **Battle Dojo Improvements**: Replaces GamePad/TV icons with P1/P2 labels, enables full 60 FPS split-view rendering, and automatically switches the host mouse cursor appropriately.
- **Zero Base-Port Modifications**: Uses the `SplatPC` runtime C++ Modding API (`SPLAT_DEFINE_GUEST_HOOK` + `SplatModHost`) with no changes to `.manifest` files.

---

## Installation
1. Copy `online_coop.dll` (Windows) or `online_coop.so` (Linux) into a `mods/` folder right next to `Splatoon.exe` / `Splatoon` (or set `RBREW_MODS_DIR`).
2. Launch SplatPC and press **ESC -> Mods -> Online Co-op**.
=======
1. Download `online_coop.dll` (Windows) or `online_coop.so` (Linux) from the repository's **Releases** (or the latest **Actions** workflow artifacts), or build from source below.
2. Copy the binary into a `mods/` folder right next to `Splatoon.exe` / `Splatoon` (or set `RBREW_MODS_DIR`).
3. Launch SplatPC and press **ESC → Mods → Online Co-op**.

To join a friend's host directly from the command line:

```text
Splatoon.exe --join <host-ip>[:port]
```

*(Or set `RBREW_NET_HOST=1` / `RBREW_NET_HOST=<port>` or `RBREW_NET_JOIN=<ip>[:port]`.)*

Settings are stored next to the mod DLL in `mods/online_coop.ini`, while Guest mode inherits keybindings, mouse sensitivity, and audio volume from SplatPC's `settings.ini`.

---

## CI & Releases

Every push and pull request runs GitHub Actions ([`.github/workflows/build.yml`](.github/workflows/build.yml)) to build, test, and upload `online_coop.dll` and `online_coop.so` as workflow artifacts. Pushing a version tag such as `v1.0.0` automatically creates a GitHub Release with both binaries and zip/tar archives attached.

---

## Building from Source

Requires CMake 3.18+, a C++20 compiler (MSVC, LLVM-MinGW, GCC, or Clang), and SDL2 (`SDL2::SDL2-static` or `SDL2::SDL2`):

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="path/to/sdl2-vulkan"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The built plugin lands at `build/Release/online_coop.dll` (or `build/online_coop.so` on Linux).
