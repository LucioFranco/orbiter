# NASSP on macOS: Race Plan (Private Experiment)

> Companion to [`rust-rewrite-investigation.md`](./rust-rewrite-investigation.md).
>
> **Framing:**
> - This is a **private, local-only experiment**. Nothing here is intended for upstream, so compatibility with upstream Orbiter, Orbiter 2016 binaries or the NASSP maintainers' build is **not** a constraint. APIs can be broken, and NASSP call sites edited directly.
> - **Targets:** macOS (Apple Silicon) first, with **Windows and Linux on the same code path**, no per-OS renderer.
> - **Priority:** time until NASSP flies on a Mac.
>
> **Sources examined:**
> - `orbitersim/orbiter@4137930` (this branch).
> - `orbiternassp/NASSP@1eb12c8` (`Orbiter2016`, 2026-09-29).
> - CaptainSwag101's NASSP `CMake` branch (`96b6380`, 2026-05-30; head of NASSP PR #1288).
> - TheGondos's `orbiter@linux` (`ad96494`, 2024-05-17) and `NASSP@4c2de5a` (2023-07-05).

---

## 1. The Plan in One Paragraph

Take **this repo's current Orbiter core** and **current NASSP**, starting from CaptainSwag101's CMake/x64 branch, and put them in **one tree with one CMake build**. Replace every Windows-only subsystem with a single cross-platform stack:
- **SDL3** for windows and input;
- a new **Rust `wgpu` graphics client** (Metal on macOS, Vulkan on Linux, DX12 on Windows);
- **miniaudio** for sound;
- `dlopen`/`LoadLibrary` for modules;
- a **Win32 type/CRT shim header** so the 400k lines of NASSP compile with minimal edits.

Cut everything not needed to fly: the Launchpad, D3D9Client, HTML help, Utils, the NASSP Configurator, and initially sound and joystick. **Grow the renderer in levels**:
1. **L1:** 2D panels, MFDs, meshes and simple planets. This is enough to fly Apollo on panels.
2. **L2:** Moon-landing-grade terrain, VC, FDAI, sound and joystick.
3. **L3:** eye candy.

**The renderer is the critical path; everything else runs in parallel with it.**

---

## 2. Key Decisions (Made for Speed)

| Decision | Choice | Why |
|---|---|---|
| Base code | **Current** Orbiter (this branch) plus **current** NASSP via CaptainSwag101's `CMake` branch | Newest code with CMake already done. Avoids a 2–3-year merge debt |
| TheGondos's Linux port | **Reference only; cherry-pick ideas and fixes** | His NASSP is 3 years stale and his core fork is ~2.5 years old. His 2D FDAI is commented out. His OpenGL client (GL 3.3, runs on macOS but deprecated) is self-described as needing a rewrite, with a broken tile loader |
| Repo layout | This repo plus `Addons/NASSP` as a **git submodule** pointing at your NASSP fork (branch `macos`) | NASSP is 1.2 GB with textures, so keep it out of this repo's history. Edit both freely |
| Engine packaging | Core becomes a **shared library** (`liborbiter.dylib/.so/.dll`) plus a thin `orbiter` executable. Addons link the library | Avoids "link against the executable" tricks (`-bundle_loader`, `-rdynamic`, import libs from an `.exe`), with the same model on all three OSes |
| Addon ABI | **Whatever compiles.** Same C++ SDK headers, now platform-neutral. Not compatible with any existing binary | Everything is built from source together |
| Win32-isms in SDK and NASSP | **Shim header** (`win32_compat.h`): `DWORD=uint32_t`, `BOOL`, `RECT`, `POINT`, `RGB()`, `HINSTANCE=void*`, `MAX_PATH`, `_stricmp→strcasecmp`, `sprintf_s→snprintf`, `strcpy_s`, `__int64`, `ZeroMemory`, … | Clears most of the ~1,800 MSVC-isms in NASSP without touching call sites |
| Windowing and input | **SDL3** in the core (window, keyboard, mouse, gamepad). Map SDL scancodes to Orbiter's `OAPI_KEY_*` (DIK) codes | One path on all OSes. Hands the native surface (Metal layer, HWND, Wayland/X11) to `wgpu` |
| Renderer | **Rust `wgpu` client** as a static library inside a thin C++ `GraphicsClient` plugin (C ABI between them, built via **Corrosion** in CMake) | Required for macOS anyway. Same code gives Vulkan/DX12 on Linux/Windows. Keeps Rust in scope |
| ImGui | Core keeps Dear ImGui with `imgui_impl_sdl3` for input. **The Rust client draws `ImDrawData`** (vertex, index and command lists over FFI, about 300 lines) | No C++ GPU backend needed |
| Sound | **Phase L1: off.** L2: XRSound's API re-backed by **miniaudio** | Not needed to fly |
| Launchpad | **Deleted.** Launch with `orbiter --scenario <path>`; add a tiny ImGui scenario picker later | 105 Win32 dialogs cut. The biggest core-side saving |
| D3D9Client | **Not built.** Optionally built on Windows only, as a visual reference | Nothing to port |
| Joystick (NASSP VESIM, RHC/THC) | L1: keyboard and mouse. L2: SDL3 gamepad/joystick behind VESIM | VESIM is already the seam |
| Telemetry sockets | Winsock → BSD sockets via a ~50-line shim (or stub in L1) | Trivial |
| FDAI (2D panel ball) | **CPU software rasterizer** (180×180 textured sphere) plus a new `oapiUpdateSurface(surf, rgba, pitch)` API | Removes WGL/GDI. The VC FDAI is already a mesh |
| Panel bitmaps in DLL resources | Script converts `.rc` (`IDB_x BITMAP "file.bmp"`) into a generated `id → path` table; `LOADBMP(id)` becomes `oapiLoadSurfaceEx(path)` | 62 call sites, zero hand edits |
| `oapiRegisterPanelBackground(HBITMAP)` | **Change the signature** to take `SURFHANDLE` and edit NASSP's 35 calls | We own both sides |
| Paths | VFS in the core: `\` → `/`, case-insensitive lookup | Needed on Linux (case-sensitive); harmless on macOS/Windows |
| 64-bit and LP64 | clang everywhere, with `-Werror=pointer-to-int-cast` and friends. `long` audit in serialisation and yaAGC | NASSP never shipped 64-bit; macOS `long` is 64-bit, unlike Windows x64 |

---

## 3. What We Are Porting (Measured)

### NASSP (~400k LOC, 46 DLLs)
Windows-specific code is **narrow**: about 15 files beyond mechanical CRT changes.

| Item | Where | Race fix | Level |
|---|---|---|---|
| MSVC CRT-isms (~1,800 occurrences in 121 files) | everywhere | `win32_compat.h` | L1 |
| Resource bitmaps (`LoadBitmap(hDLL, MAKEINTRESOURCE)`, 62 sites, 226+ `.rc` entries) | `saturnpanel.cpp`, `lempanel.cpp`, MFDs, … | Generated table plus `oapiLoadSurfaceEx` | L1 |
| Panel backgrounds (`HBITMAP`, 35 calls) | CSM/LM panels | API changed to `SURFHANDLE` | L1 |
| Winsock | `ProjectApolloMFD.cpp`, `ARCore.cpp`, `csm_telecom.*`, `lm_telecom.*` | Stub (L1), then BSD sockets | L1/L2 |
| DirectSound | `soundevents.cpp` `InitDirectSound` | **Dead code: delete** | L1 |
| Debug BMP dump (GDI) | `scs.cpp:5478-5600` | `#ifdef _WIN32` | L1 |
| Lua 5.1 used directly | `saturn.cpp` | Link the engine's Lua | L1 |
| Win32 Configurator (Launchpad tabs) | `ProjectApolloConfigurator.cpp` | **Excluded from build**; edit cfg files by hand | — |
| 2D FDAI (WGL into a DIB, then `oapiGetDC`) | `src_sys/FDAI.cpp` | Stub (L1: frame and needles only, as Gondos did), then CPU rasterizer plus `oapiUpdateSurface` | L2 |
| DirectInput joysticks | `saturn.cpp`, `LEM.cpp`, `vesim.cpp` | Compiled out (L1), then SDL3 in VESIM | L2 |
| XRSound (via irrKlang) | `soundlib.cpp` | Null backend (L1), then miniaudio | L2 |
| Excel checklists (`BasicExcelVC6`) | checklist controller | Already UTF-16-safe; fix `__int64` | L1 |
| yaAGC / yaAEA | `src_sys/yaAGC`, `src_lm/yaAGS` | Portable C upstream; audit `long` | L1 |
| Threads | `std::thread` | Already portable | — |

### Orbiter core (keep; de-Win32)
| Item | Race fix |
|---|---|
| `WinMain`, message loop, `HWND` | SDL3 event loop; window owned by the core; native handle passed to the graphics client (API changed freely) |
| Launchpad (Win32 dialogs) | Delete. CLI launch |
| DirectInput (keyboard/joystick) | SDL3 |
| `LoadLibrary`/`GetProcAddress` | `dlopen`/`dlsym` wrapper; `.dylib`/`.so`/`.dll` suffix |
| WIC image I/O, `HBITMAP` helpers | `stb_image` / `stb_image_write` |
| HTML Help, registry (Wine check), `timeBeginPeriod` | Delete |
| D3D7 types in `Baseobj.cpp`/`Mesh.h` (`d3d.h`) | Tiny replacement header with the structs |
| `CreateThread` (scenario watcher, console) | `std::thread`, or delete (Launchpad gone) |
| Paths and case | VFS layer |

---

## 4. Renderer Scope by Level

The Rust client implements `oapi::GraphicsClient` through a thin C++ plugin that forwards to Rust over a C ABI.

| Level | Must render | Enough for |
|---|---|---|
| **L1: "It flies"** | Surfaces as textures; **blits with colour key, batched per target** (NASSP issues thousands per frame); **Sketchpad** (lines, polys, ellipses, text with a font map for Arial/Courier/Sans/MOCR); ImGui draw data; `.msh` meshes with materials, textures and animations; planets as textured spheres from level ≤ 8 tiles; star field; 2D panel composition; HUD; MFDs | Saturn V launch to orbit, TLI and burns flown from **2D panels**, with AGC/DSKY, RTCC MFD and checklists |
| **L2: "Moon mission"** | Quadtree tile loader (`.tree` archives, BC textures uploaded directly), **elevation meshes** for lunar landing; **virtual cockpit** (dynamic textures, `clbkVCRedrawEvent`, mouse picking); `SetMeshMaterialEx` for emissive VC lighting (replacing NASSP's `gcCore` calls); local lights; exhaust particles; FDAI pixel upload | Apollo 11 end-to-end |
| **L3: "Pretty"** | Atmosphere scattering, clouds, shadows, glare, PBR, env maps | Looks like D3D9Client |

**Planned from day one:** camera-relative float rendering (the core already provides double-precision state), reversed-Z with an infinite far plane, and one render pass that composes all 2D panel surfaces.

---

## 5. Parallel Tracks and Timeline

Rough numbers, for **one focused engineer**; two people (core+NASSP / renderer) roughly halves the calendar. These are estimates, not measurements.

```
Week:        1   2   3   4   5   6   7   8   9  10  11  12 ...  ~24
Track A  [core: SDL3, shims, VFS, dylib, CLI launch, headless on mac]
(core)               [NASSP: submodule, CMake for clang, compat hdr,
                      bitmap table, stubs → headless Saturn V run]
Track B  [bridge C ABI][wgpu window + ImGui][surfaces/blits/Sketchpad]
(render)                           [meshes, spheres, stars, panels]
                                               ▲ L1 "It flies" on macOS (~wk 10–12)
                                                        [tiles+elev, VC, FDAI,
                                                         sound, joystick] ▲ L2 (~wk 24)
CI       [macOS arm64 + Linux + Windows matrix from week 1, headless smoke test]
```

| Milestone | Exit test (all three OSes in CI; interactive checks on macOS) |
|---|---|
| **A1: Core headless** (~wk 3–4) | `orbiter --scenario "Delta-glider/…" --fixedstep 0.02 --maxframes 5000` runs; state matches a Windows reference within tolerance |
| **A2: NASSP headless** (~wk 6–8) | All NASSP modules build and load; Saturn V launch to orbit headless; AGC runs; `CurrentState.scn` sane |
| **B1: Window** (~wk 3) | `wgpu` window via SDL3 on Metal; ImGui dialogs from the core render |
| **B2: 2D** (~wk 6–7) | Stock HUD and MFDs render; blit/colour-key test scene matches D3D9Client screenshots (Windows) |
| **L1** (~wk 10–12) | NASSP CSM 2D panels usable; launch to orbit flown on macOS |
| **L2** (~wk 20–26) | Lunar landing with terrain; VC; sound; joystick |

**Windows and Linux ride along:** the same SDL3, `wgpu` and miniaudio code runs everywhere. The platform-specific items are the `dlopen` suffix, `wgpu` surface creation from SDL's native handle, and Linux case-sensitivity (handled by the VFS).

---

## 6. Race Risks

1. **LP64 and 64-bit bugs in NASSP.** It never shipped 64-bit, and Gondos saw a VC camera shift on 64-bit Linux. *Mitigation:* A2 headless first, with UBSan/ASan builds on Linux.
2. **Panel blit throughput.** Thousands of small colour-keyed blits per frame. *Mitigation:* batch per target surface from the first implementation; never submit per blit.
3. **Font metrics.** MOCR/RTCC and panel text were laid out for GDI fonts. *Mitigation:* bundle fonts and keep a face-mapping table; accept small visual diffs.
4. **FFI surface.** `Sketchpad` has 74 virtuals and `GraphicsClient` 79, and many are called per frame. *Mitigation:* the C++ shim records draw commands into a buffer and Rust consumes one batch per surface per frame, instead of one FFI call per primitive.
5. **Numerical divergence.** Comparisons against Windows use tolerances. Build with `-ffp-contract=off` when comparing.
6. **Textures and assets.** NASSP's 4K/8K textures and Orbiter's planet textures come from release zips, not git. The local setup script needs to fetch or point at them (`ORBITER_PLANET_TEXTURE_INSTALL_DIR` already exists).

---

## 7. First Steps (This Week)

1. **Tree setup:** add `Addons/NASSP` as a submodule (your NASSP fork, branch `macos`, based on CaptainSwag101's `CMake`). Add a top-level CMake option to build it. Add a CI matrix (macOS arm64, Ubuntu, Windows) with a headless smoke test.
2. **`win32_compat.h` plus portable SDK headers:** get `OrbiterAPI.h`/`VesselAPI.h` compiling with Apple clang, then compile NASSP's `PanelSDK` (almost pure computation) against them. This proves the shim approach in about a day.
3. **Core de-Win32 spike:** SDL3 main loop, delete the Launchpad, `dlopen` loader, `liborbiter` shared library, then run headless on macOS (A1).
4. **Renderer spike:** C-ABI `GraphicsClient` bridge, Rust crate via Corrosion, and a `wgpu` surface from an SDL3 window (Metal layer on macOS). Then clear the screen and draw `ImDrawData` (B1).

If useful, I can start on steps 1 and 2 right away. The shim header and portable SDK headers are the foundation both tracks depend on.
