# Orbiter Engine Deep Dive: Rust Rewrite and Cross-Platform Port (macOS First)

> **Scope.** This report explains how the Orbiter engine works, from process startup to pixels on screen. Its purpose is to inform a rewrite of the core engine and/or graphics in Rust that runs on **macOS (priority), Linux and Windows**.
>
> **Snapshot.** The fork branch was fast-forwarded to upstream `orbitersim/orbiter@4137930` (merge of PR #679, 2026-09-24). The fork was 41 commits behind. All file/line references in this report are to that commit.
>
> **Follow-up:** with the goal narrowed to *running NASSP natively on macOS*, see [`nassp-macos-recommendation.md`](./nassp-macos-recommendation.md). It re-orders the roadmap below: a Rust renderer first, the C++ core ported rather than rewritten, and the Rust core deferred with NASSP as its conformance suite.

---

## 0. TL;DR

1. **Orbiter is two engines behind one C++ ABI.** `Orbiter.exe` (≈85k LOC, MIT) is a *headless-capable* simulation core. It has no renderer of its own and is built with `NOGRAPHICS`. Rendering is supplied by a **graphics client plugin DLL**. Today that is `D3D9Client` (≈68k LOC C++ plus ≈6.7k LOC HLSL, LGPL). The client registers itself through `oapiRegisterGraphicsClient()` and implements the `oapi::GraphicsClient` virtual interface.
2. **The addon ecosystem is the real constraint, not the engine.** Addons subclass exported C++ classes (`VESSEL2/3/4`, `oapi::Module`, `MFD2`, `CELBODY2`, `GraphicsClient`, `ImGuiDialog`) and call about 335 `oapi*` free functions and about 367 `VESSEL` methods. These are linked against **MSVC-mangled symbols exported from `Orbiter.exe`**; CI diffs them against a list of 1,146 symbols from Orbiter 2016 (`exports.2016.txt`). **On macOS (arm64) no existing binary addon can ever load natively.** The best achievable goal is *source* compatibility.
3. **Physics is compact, deterministic-ish and very portable.** It is a single thread with double-buffered `s0/s1` state vectors, adaptive RK2–RK8 and symplectic integrators with sub-stepping, Encke orbit stabilisation, VSOP87/ELP82 ephemerides, J-coefficient and Pines spherical-harmonic gravity, and a spring-damper touchdown model. It reads like a textbook and is a good candidate for a faithful Rust port. The core can already run **headless with a fixed step and a frame limit** (`--fixedstep`, `--maxframes`, `srand(12345)`). The C++ build can therefore act as a **golden-master oracle** for a Rust reimplementation.
4. **Graphics is the most Windows-bound part and the best place for Rust to start.** D3D9Client depends on D3D9, D3DX (effects framework, math, texture I/O), SM3.0 `.fx` techniques and GDI. It has to be rewritten for macOS anyway, because Metal is the only first-class API there. `wgpu` (Metal/Vulkan/DX12) is the natural Rust choice. The core already does all the double-precision work and hands the client a clean callback surface, so a Rust renderer can be dropped in as a new graphics client.
5. **Recommendation:** an **incremental "strangler" path**, not a big-bang rewrite:
   - **Phase 0:** De-Win32 the C++ core (SDL3 or winit, ImGui launchpad, no DirectInput, portable paths, `DWORD` width) so it builds natively on macOS.
   - **Phase 1:** A new **Rust `wgpu` graphics client** behind a thin C ABI bridge. This gives a native macOS build with stock content.
   - **Phase 2:** A **Rust simulation core**, validated frame-by-frame against the C++ core, exposing a **stable C ABI** plus a **header-only C++ compatibility SDK**. Existing open-source addons then recompile unchanged or nearly unchanged.
   - **Phase 3:** Retire the C++ core, then optionally add a Rust-native addon SDK.

   This keeps a shippable product at every step, and it keeps the 20-year content ecosystem (meshes, textures, scenarios, configs, open-source vessels such as NASSP and the XR series).

---

## 1. Codebase Map

| Area | Path | LOC (C/C++/HLSL) | License | Notes |
|---|---|---:|---|---|
| Simulation core + UI | `Src/Orbiter` | ~84.6k | MIT | `Orbiter.exe`: physics, scenario, camera, HUD/MFD/panel *logic*, launchpad, dialogs |
| Public SDK headers | `Orbitersdk/include` | ~21.6k (mostly docs) | MIT | `OrbiterAPI.h` (7.7k), `VesselAPI.h` (6.5k), `GraphicsAPI.h`, `DrawAPI.h`, `MFDAPI.h`, `ModuleAPI.h`, `CelBodyAPI.h` |
| SDK static lib | `Src/Orbitersdk` | tiny | MIT | `DllMain`, `GetModuleVersion`, plus ImGui/ImPlot compiled into every addon |
| D3D9 graphics client | `OVP/D3D9Client` | ~68k + 6.7k shaders | LGPL/GPL dual | The only maintained renderer |
| Celestial body modules | `Src/Celbody` | ~15.4k | MIT | VSOP87 planets, ELP82 Moon, Lieske Galilean moons, SatSat, atmospheres (J71G, NRLMSISE-00, Mars2006) |
| Stock vessels | `Src/Vessel` | ~44k | MIT | DeltaGlider, Atlantis, ShuttleA, ISS, HST, etc. These are the de-facto API test suite |
| Plugins | `Src/Plugin` | ~18k | MIT | TransX, ScnEditor, ExtMFD, LuaConsole/LuaMFD, Rcontrol, TrackIR, … |
| Lua scripting | `Src/Module/LuaScript` | ~21.5k | MIT | `Interpreter.cpp` (11k) plus `lua_vessel_mtd.cpp` (10k); a hand-written binding of most of the API |
| Sound | `Sound/XRSound` | ~7.6k | MIT (code) | Uses **irrKlang** (proprietary binary) |
| Utilities | `Utils` | ~30k | MIT | meshc, tileedit (Qt), texpack, plsplit, Shipedit… |
| Third party | `Extern` | — | various | Lua 5.1, zlib, ImGui/ImPlot (FetchContent), LuaFileSystem, Penlight, Htmlhelp |

First-party code totals about **318k LOC**. Data assets are the larger share of the value: `.msh` meshes, `.dds` textures, `.tree` tile archives, `.cfg` / `.scn` text files, and gravity models.

---

## 2. Process and Module Architecture

```
Orbiter.exe (NOGRAPHICS core; exports C++ API + GImGui)
 ├─ Launchpad (Win32 dialogs from Orbiter.rc; 105 DIALOG resources)
 ├─ Modules\Plugin\*.dll       ← oapi::Module plugins (InitModule → oapiRegisterModule)
 │    └─ D3D9Client.dll        ← a Module that calls oapiRegisterGraphicsClient()
 ├─ Modules\Startup\*.dll      ← always-loaded plugins
 ├─ Modules\<Vessel>.dll       ← per-vessel-class code: ovcInit/ovcExit, loaded when a .cfg says Module=
 ├─ Modules\Celbody\*.dll      ← ephemeris/atmosphere modules (CELBODY2 / ATMOSPHERE)
 └─ LuaInterpreter.dll         ← script engine used by Script vessels, MFDs, console, scenarios
```

**Startup** (`Src/Orbiter/Orbiter.cpp:170` `WinMain`):
1. Fixes up the working directory. All data paths are *relative to the CWD*: `Config\`, `Meshes\`, `Textures\`, `Scenarios\`.
2. Appends `;Modules` to `PATH` so DLL dependencies resolve.
3. Parses the command line (`cmdline.cpp:168-178`). The flags include `--scenario`, `--fixedstep`, `--maxframes`, `--maxsimtime` and `--plugin`.
4. Seeds the RNG with a constant: `srand(12345)`.
5. `Orbiter::Create` (`:371`) loads `Orbiter.cfg`, starts DirectInput, reads `keymap.cfg` and builds the Launchpad. It then loads plugin DLLs with `LoadLibrary`, looks up `GetModuleVersion` and `InitModule`, and adds `Modules\Startup`.
6. `Run()` (`:1015`) is a classic `PeekMessage` game loop.

**Session start** (`CreateRenderWindow`, `:691`):
1. Asks the graphics client for a window (`clbkCreateRenderWindow`). If no client is attached, a text console (`ConsoleNG`) is opened instead, which is **headless mode**.
2. Creates the DirectInput keyboard and joystick devices, the `DialogManager` (ImGui) and the `PlanetarySystem` (from `Config\Sol.cfg` and each body's `.cfg`).
3. Calls `InitState(scenario)`, sets the focus vessel and initialises the camera.
4. Calls each plugin's `opcLoadState`, `gclient->clbkPostCreation`, `psys->PostCreation` and `Module::clbkSimulationStart`, then initialises the `Pane` (HUD/MFD/panels).

**Session end** normally saves `CurrentState.scn` and tears everything down. It can also be configured to `exit(0)` or to **re-spawn the process** (`_execl("orbiter.exe")`), a hint at how much global state is involved.

### Global state
The core is built around process-wide singletons. Approximate references: `g_pOrbiter` (563), `g_psys` (312), `g_camera` (243), `g_focusobj` (192) and `g_pane` (166). `extern TimeData td` is used in 38 files. Some physics routines hold `static` scratch buffers, for example `Vessel::AddSurfaceForces`, so **nothing is re-entrant or thread-safe**. A Rust port gets a chance to fix this structurally.

---

## 3. The Frame: Time, Update, Render

```
Run() loop (Orbiter.cpp:1015)
 └─ if no Win32 message pending and session active:
     BeginTimeStep(running)                      :1745
       deltat = steady_clock delta (first 3 frames forced to 10 ms), clamp ≤ 0.1 s
       td.BeginStep(deltat) → SimDT = deltat*warp (or fixed step), SimT1, MJD1
     UpdateWorld()                               :1951
       ModulePreStep: Module::clbkPreStep, VESSEL2::clbkPreStep for every vessel
       FRecorder_Play (if playback)
       g_psys->Update()   ← physics: fills s1 for all bodies
       DialogManager::UpdateDialogs
       ModulePostStep: clbkPostStep (vessels, then modules)
       KillVessels()      ← deferred deletion
     EndTimeStep(running)                        :1787
       g_psys->FinaliseUpdate()  ← swap s1→s0 (EndStateUpdate), PostUpdate
       td.EndStep()               ← T1→T0
       g_camera->Update(); g_pane->Update()  (HUD/MFD/panel logic)
       gclient->clbkUpdate(running)
     UserInput()   ← DirectInput keyboard/joystick → vessels, camera, keymap
 └─ Render3DEnvironment()                        :985
       DialogManager::ImGuiNewFrame
       gclient->clbkRenderScene()     ← renderer draws world + calls back Render2DOverlay
       Output2DData()
       gclient->clbkImGuiRenderDrawData()
       gclient->clbkDisplayFrame()    ← Present
```

Key properties:
- **Variable real-time step × warp** (warp is clamped to 0.1×–100,000×, `Orbiter.cpp:89-90`). The step is fixed only when `--fixedstep` or the config says so. Physics and rendering run **in lock-step on one thread**, and there is no interpolation between physics states for rendering.
- **Time precision:** `SimT1` is stored as offset plus increment to avoid round-off after about 1e6 s (`TimeData.h`). MJD is carried alongside.
- **Two-phase update:** every `Body` has `sv[2]` with `s0` (published state at `SimT0`) and `s1` (state under construction at `SimT1`). During `Update`, bodies read others' `s0`, or interpolate celestial `s1` via `InterpolatePosition(tfrac)`. They write only their own `s1`. `EndStateUpdate` swaps the two. This structure maps well to Rust's ownership model: an immutable "previous world" plus a mutable "next world".

---

## 4. Simulation Core in Detail

### 4.1 Class hierarchy
```
Body (name, mass, size, s0/s1 StateVectors{pos,vel,R,Q,omega}, hVis)
 └─ RigidBody (PMI, propagators, gravity sources list, Elements, Encke)
     ├─ CelestialBody (ephemeris via CELBODY module, precession, rotation, J-coeffs, Pines)
     │    ├─ Star
     │    └─ Planet (atmosphere, elevation manager, bases, clouds/rings params, nav beacons, labels)
     └─ VesselBase
          └─ Vessel (9k LOC: thrusters, tanks, airfoils, ctrl surfaces, docking, attachments,
                     touchdown, animations, meshes, exhaust/particles, lights, beacons, nav radios,
                     MFD/HUD/panel hooks, flight recorder, module interface VESSEL*)
SuperVessel  — composite of docked vessels treated as one rigid body
Base         — surface base: objects (Baseobj.cpp) + landing pads, attached to a Planet
PlanetarySystem (Psys.cpp) — owns all of the above and orders the update
```

### 4.2 Update order (`Psys.cpp:815`)
```
for body: BeginStateUpdate()                 // s1 := valid copy
for star: RelTrueAndBaryState(); AbsTrueState()   // recursive, barycentre-aware ephemerides
for celestial: Update()                      // planets/moons: ephemeris or dynamic integration
for vessel: UpdateBodyForces()               // thrust, aero, radiation → Flin/Amom accumulators
for supervessel: Update()                    // composites integrated as one body
for vessel: Update()                         // RigidBody::Update (integrator) or landed shortcut
```

### 4.3 Celestial mechanics
- **Ephemerides** are supplied by `CELBODY2` modules through `clbkEphemeris(mjd, req, res)` and `clbkFastEphemeris(simt, req, res)`, returning true or barycentric positions and velocities. Implementations include VSOP87 series (`Src/Celbody/Vsop87`, one directory per planet), ELP82 for the Moon, Lieske E5 for the Galilean moons (`Galsat`) and a Saturn satellites module (`Satsat`). `ErrorLimit` and `SamplingInterval` in each `.cfg` control series truncation and interpolation.
- **Fallback:** osculating elements from the `.cfg`, giving a Kepler orbit. This matters: the most recent upstream commit (`d5c4887`) exists because **8 moons only ship 32-bit ephemeris DLLs**. In 64-bit builds these silently failed to load, which gave vessels near Mars and Uranus random kicks. Binary-only modules are a liability even on Windows.
- **Barycentres:** bodies can be computed relative to the parent's barycentre (`EPHEM_PARENTBARY`); `RelTrueAndBaryState` recurses through secondaries.
- **Rotation and precession:** sidereal period, obliquity, LAN, and a precession period and reference axis (`UpdatePrecession`, `UpdateRotation`). For example, Earth precesses with a period of −9,413,040 days.
- **Gravity:** `PlanetarySystem::Gacc_intermediate` sums point masses from a per-body *gravity source list* (up to 20 sources). The list is rescanned at randomised intervals. Non-spherical terms come from either J2..Jn zonal coefficients or **Pines' algorithm** with full spherical-harmonic models (`GravityModels/egm96_to360.tab`, lunar/Mars/Venus/Mercury `.tab` files, `GravCoeffCutoff` in `.cfg`).
- **Solar radiation pressure:** `GetMomentumFlux` models a single source at the origin with fixed solar luminosity. Vessels can override the force with `clbkGetRadiationForce`.

### 4.4 Rigid-body integration (`Rigidbody.cpp:182`, `BodyIntegrator.cpp`)
- **Linear plus angular state propagated together.** The available integrators are RK2, RK4, RK5–RK8 (a generic Butcher-tableau driver `RKdrv_LinAng`) and symplectic SY2, SY4, SY6 and SY8.
- **Adaptive propagator levels.** `PropMode[level]` holds a propagator plus a time/angle step target and limit. `SetPropagator` chooses the level and a **sub-step count** from the orbit fraction per frame (`ostep`) and angular rates. The defaults are `{RK2, RK4, RK6, RK8, RK8}` (`Config.cpp:55`), and users can override them.
- **Encke stabilisation.** When one body dominates gravity and the step covers a large fraction of the orbit, the state is propagated as a 2-body Kepler orbit plus integrated perturbations (`RigidBody::Encke`). This is what keeps orbits stable at high time warp.
- **Round-off control.** `rpos_base + rpos_add` and `rvel_base + rvel_add` are kept separately and flushed every 1,000 steps. Positions are heliocentric doubles in metres, around 1e12 m at the outer planets.
- **Angular dynamics.** The full Euler equations are used (`EulerInv_full`), with simplified and zero variants. Gravity-gradient torque and tidal damping are applied, and angular velocity is hard-clamped to 100 Hz as a numerical safety net.
- `ValidateStateUpdate` lets `Vessel` reject a step and retry (used for ground contact).

### 4.5 Vessel physics (`Vessel.cpp`)
- **Propulsion:** `ThrustSpec` (position, direction, maximum thrust, Isp with pressure dependence, tank) and `ThrustGroupSpec` (main, retro, hover, 14 RCS groups, user groups). There are propellant `TankSpec`s, and mass is updated each step (`UpdateMass`).
- **Aerodynamics:** airfoils defined by **user callback functions** (`AirfoilCoeffFunc` / `Ex` / `Ex2`) that return Cl, Cm and Cd as functions of AoA, Mach and Re. There are also control surfaces with actuator delays and variable drag elements. The atmosphere is supplied by the planet's `ATMOSPHERE` module, with wind vectors.
- **Surface contact:** there are two models.
  1. The **legacy 3-point** model: tilt correction and altitude snap, as shown in the inline code in `Vessel::Update` (`:4717`).
  2. **Dynamic touchdown points** (`TOUCHDOWNVTX`: stiffness, damping, friction; any number of points), evaluated as forces inside the integrator sub-steps (`AddSurfaceForces`, `:4289`).

  Terrain elevation comes from the **same tile archives the renderer uses**, read by the core's `ElevationManager` (`elevmgr.cpp`) through `ZTreeMgr`. When landed, vessels switch to a cheap kinematic "stuck to the rotating surface" update.
- **Composites:** docking merges vessels into a `SuperVessel`, which has a single rigid body with combined mass and inertia. Attachments (parent/child) slave the child's state to the parent (`UpdateAttachments`).
- **Navigation radios:** VOR, VTOL, ILS, IDS (docking) and XPDR, with frequency channels. Beacons are defined in planet and base `.cfg` files.
- **Flight recorder and playback:** attitude and state sample streams plus system event streams, with an in-sim editor.

### 4.6 Data formats (the compatibility surface that matters most)
| Format | Example | Parser |
|---|---|---|
| Planetary system cfg | `Src/Celbody/Sol/Config/Sol.cfg` | `Psys.cpp` (`Planet1 = Mercury`, `Earth:Moon1 = Moon`) |
| Body cfg | `.../Earth/Config/Earth.cfg` | `Celbody.cpp`, `Planet.cpp`: physical constants, atmosphere, tiles, observers, nav beacons |
| Vessel class cfg | `Config/Vessels/*.cfg` | `Vessel::SetClassCaps`: `Module=`, mass, PMI, meshes |
| Scenario | `Scenarios/**/*.scn` | `State.cpp`, `Psys::InitState`, `clbkLoadStateEx`. `BEGIN_ENVIRONMENT`/`FOCUS`/`CAMERA`/`HUD`/`MFD`/`SHIPS` blocks; each vessel block is free-form, owned by its module |
| Mesh | `Meshes/*.msh` | `Mesh.cpp:828`. Text `MSHX1`: GROUPS/MATERIAL/TEXTURE/GEOM (pos, normal, uv), MATERIALS, TEXTURES |
| Textures | `*.dds` | DXT1/3/5 (BC1–3), loaded by the graphics client |
| Tile archives | `Textures/<Planet>/Archive/*.tree` | `ZTreeMgr`: a quadtree TOC plus zlib-deflated tiles. Layers: surf, mask, elev, elevmod, label, cloud. Elevation tiles are INT16 259×259 with an `ELEVFILEHEADER` |
| Gravity models | `GravityModels/*.tab` | `PinesGrav.cpp` |
| Keymap and config | `keymap.cfg`, `Orbiter.cfg` | `Keymap.cpp`, `Config.cpp` |

All of these are plain text or simple binary and **platform-neutral**, apart from backslash paths (see §7). A rewrite should treat them as a frozen contract.

---

## 5. Addon / Plugin API: the Hard Constraint

### 5.1 Module types
| Kind | Entry points | Base class |
|---|---|---|
| Vessel module | `ovcInit(OBJHANDLE, int flightmodel) → VESSEL*`, `ovcExit`, `InitModule`/`ExitModule` | `VESSEL2`/`VESSEL3`/`VESSEL4` (callbacks: `clbkSetClassCaps`, `clbkPreStep`, `clbkPostStep`, `clbkLoadStateEx`, `clbkSaveState`, `clbkLoadPanel2D`, `clbkLoadVC`, `clbkDrawHUD`, `clbkConsumeBufferedKey`, `clbkAnimate`, `clbkDockEvent`, …) |
| Plugin | `InitModule(HINSTANCE)` → `oapiRegisterModule(new MyModule)` | `oapi::Module` (`clbkSimulationStart/End`, `clbkPreStep`/`PostStep`, `clbkNewVessel`, `clbkTimeJump`, `clbkPause`, …) |
| Graphics client | as plugin, then `oapiRegisterGraphicsClient` (`GraphicsAPI.cpp:908`) | `oapi::GraphicsClient : Module` (79 virtuals) |
| Celestial body | `InitInstance`/`ExitInstance` | `CELBODY2`, `ATMOSPHERE` |
| MFD mode | `oapiRegisterMFDMode` | `MFD2` / `GraphMFD` |
| Script | Lua 5.1 via `LuaInterpreter.dll` | `oapiCreateInterpreter`, Script vessels |

### 5.2 What the ABI actually is
- `OAPIFUNC` is `__declspec(dllexport)` inside `Orbiter.exe` and `__declspec(dllimport)` in addons. **Whole C++ classes are exported** (`class OAPIFUNC VESSEL`). `VESSEL::VESSEL` just stores `vessel = (Vessel*)hvessel` (`Vessel.cpp:6231`), and every method is a thin forwarder into the core object.
- Addons link `Orbiter.lib` (the import library of the exe) and `Orbitersdk.lib`. The latter provides `DllMain`, which calls `InitLib`, plus `GetModuleVersion` (`Src/Orbitersdk/Orbitersdk.cpp`).
- The ABI is coupled to:
  - **MSVC name mangling and vtable layout.** CI compares `dumpbin /EXPORTS` against `exports.2016.txt` (1,146 symbols) for x86 builds only.
  - **MSVC STL layout.** `ImGuiDialog` has `std::string` members, and `GraphicsClient::LABELLIST` uses `std::vector`/`std::string`.
  - **ImGui version.** `GImGui` is exported from the exe as a data symbol (`DlgMgr.cpp:420`), and each addon statically compiles ImGui from `Orbitersdk.lib`.
  - **Win32 types in signatures:** `HDC` (`clbkDrawHUD(..., HDC)`, `oapiGetDC`), `HWND`, `HINSTANCE`, `HBITMAP`, `DLGPROC`, `LRESULT`.
  - **`DWORD` = 32-bit `unsigned long` on Windows**, but `unsigned long` is 64-bit on LP64 macOS/Linux. A naive `typedef` changes struct layouts and overloads.
  - **Direct memory layouts.** `NTVERTEX` and `MATERIAL` are D3D7-compatible, and `MESHGROUP*` is returned so addons can edit vertex arrays in place. Packed structs such as `VESSELSTATUS2` with a version field are cast through `void*`.
- The C++ **object model is part of the contract.** Addons subclass and override virtuals, and the core calls them. A new core must therefore instantiate addon objects and dispatch to their vtables.

### 5.3 Consequences for a rewrite
| Target | Can old binary addons load? | Can old addon *source* compile? |
|---|---|---|
| Windows x64, same MSVC ABI | Yes, if the new core exports identical mangled symbols (possible via a C++ shim DLL named/linked as `Orbiter.exe`'s import lib) | Yes |
| Linux x86_64 | No (PE vs ELF). Wine or winelib only | Yes, with a portable SDK and some fixes |
| **macOS arm64** | **Never** (PE x86 on arm64). Only via Wine plus Rosetta for the whole app | Yes, with a portable SDK and fixes (GDI HDC usage and `\\` paths must go) |

In the in-tree code, 8 vessel/plugin sources still use `HDC`/GDI, and 39 already use `oapi::Sketchpad`. The core already abstracts 2D drawing through `Sketchpad`, and GDI is legacy.

---

## 6. Graphics Architecture

### 6.1 The boundary
The core has **no renderer**. It keeps the camera, the HUD/MFD/panel *logic* and all double-precision state. The graphics client:
- creates the window and device (`clbkCreateRenderWindow`, `clbkFullscreenMode`, `clbkGetViewportSize`);
- owns **surfaces** (`SURFHANDLE`: textures and render targets), **device meshes** (`DEVMESHHANDLE`), fonts, pens and brushes;
- provides a **2D drawing context** (`oapi::Sketchpad`, 74 virtuals: lines, polygons, text, blits, transforms). The core draws the HUD and MFDs through it (`Pane.cpp`, `hud.cpp`) onto client surfaces, then asks the client to compose panels via `clbkRender2DPanel`;
- tracks visuals (`clbkNewVessel`, `clbkDeleteVessel`, `clbkVisEvent` for mesh insert/delete, animations, group edits), and reads object state through `oapi*` getters every frame;
- drives the ImGui backend (`clbkImGuiInit`, `NewFrame`, `RenderDrawData`, `clbkImGuiSurfaceTexture`);
- implements the particle streams used by the core (`clbkCreateExhaustStream`, `clbkCreateReentryStream`) and screen annotations;
- receives `GetBaseStructures` and `GetBaseShadowGeometry`. Surface-base geometry is **generated on the CPU by the core** (`Baseobj.cpp`, which still uses D3D7 types) and consumed by the client.

This boundary is the **best seam in the codebase** for introducing Rust.

### 6.2 D3D9Client internals
```
D3D9Client (GraphicsClient)          D3D9Client.cpp  (3.3k)
 ├─ CD3DFramework9                   D3D9Frame.cpp   (device, modes, MSAA, caps checks)
 ├─ Scene                            Scene.cpp       (3.9k) — per-frame orchestration
 │   ├─ vObject → vVessel (meshes, animations, env maps, shadows, exhaust, beacons)
 │   │          → vPlanet (TileManager2 / SurfTile / CloudTile, rings, haze, atmosphere)
 │   │          → vBase (base structures, runway lights), vStar
 │   ├─ CelSphere / CSphereMgr (star field, constellations, grids, background image)
 │   ├─ D3D9ParticleStream (exhaust/reentry)
 │   ├─ Shadow maps, env cubemaps, irradiance integration
 │   └─ Custom cameras (render-to-texture for addons)
 ├─ D3D9Mesh / MeshMgr / MaterialMgr (PBR & legacy materials, per-group flags)
 ├─ D3D9Pad (GPU Sketchpad), GDIPad (GDI fallback), D3D9TextMgr (glyph atlas fonts)
 ├─ SurfTile loaders (Surfmgr2.cpp) + ZTreeMgr (.tree archives) + background load thread
 ├─ IProcess (post-processing: light blur, glare, lens flare)
 ├─ WindowMgr / DebugControls / AtmoControls (in-sim tooling)
 └─ gcCore / gcConst (extension API used by addons: custom cameras, sketchmesh, poly, etc.)
```

**Frame (`Scene::RenderMainScene`, `Scene.cpp:1242`):**
1. Update vessel animations.
2. **Round-robin offscreen work**, one item per frame (`dwTurn`): custom camera views, vessel environment cubemap faces, irradiance maps.
3. Build the render list; optionally run a **normal/depth pre-pass** (used for SSAO-style effects and for sun and local-light visibility).
4. Compute Sun and local-light visibility.
5. Main pass on an HDR/float offscreen target when available:
   - the celestial sphere (stars, constellations, planetarium overlays);
   - the focus-vessel **shadow map**, early, for bases and planet surfaces;
   - **planets back-to-front with depth-range partitioning**: each gets its own near/far planes; distant ones become dots; plus labels and markers;
   - addon render callbacks (`RENDERPROC_PLANETARIUM`, `RENDERPROC_EXTERIOR`);
   - vessels grouped by shared shadow maps; exhaust, beacons, particle streams, axis vectors;
   - the internal cockpit / virtual cockpit, rendered last with its own clip planes.
6. Resolve the offscreen target to the backbuffer, then post-process (light blur/bloom).
7. Sun and light glares and lens flare.
8. The 2D overlay: `RENDERPROC_HUD_1ST`, then core `Render2DOverlay` (HUD, MFDs, 2D panels via `clbkRender2DPanel`), then `HUD_2ND`.
9. Debug views and the ImGui draw (from the core), then Present.

**Precision strategy:** all world matrices are built **camera-relative in double, then cast to float** (see `vObject::Update`), plus per-object near/far range partitioning. There are no f64 shaders, which is good news for Metal, which has no f64 at all.

**Planet renderer (`Tilemgr2.*`, `Surfmgr2.cpp`):**
- A global lat/long quadtree: level-4 roots, 256×256 texture tiles (`TILE_FILERES`), elevation grids 259×259 (with padding). Levels go up to about 19–21 (`MaxPatchResolution`).
- Layers: surface colour (DXT), water mask / night lights (mask), elevation plus user elevation modifications, labels, and clouds. These are read from compressed `.tree` archives, or loose files as a fallback.
- An async loader thread (`Tilemgr2.cpp:829`, `CreateThread`) feeds a queue. Elevation for a tile comes from its great-grandparent's file and is interpolated down.
- Per-tile meshes are generated on the CPU from elevation, with tile-local origin shift for precision.
- Atmosphere: the newer physically based scattering (`VPlanetAtmo.cpp`, `shaders/Scatter.hlsl`, `NewPlanet.hlsl`) with precomputed tables, an older haze path (`HazeMgr`), fog, cloud shadows, specular water ripple and micro-textures.

**Shaders:** about 6.7k lines of **SM3.0 HLSL**, mostly using the **D3DX Effects framework** (`.fx` techniques/passes, `ID3DXEffect`, `D3DXHANDLE` ≈300 uses) plus some raw `.hlsl` with constant tables. The effects framework has no modern equivalent, so every technique must be re-expressed as explicit pipelines and bind groups.

**D3DX dependency:** math (`D3DXMATRIX`/`VECTOR`, ~1,000 uses), texture creation and loading, surface loading, fonts and effects. It requires the **legacy June 2010 DirectX SDK**.

### 6.3 What a new renderer must reproduce (feature checklist)
Needed for stock content to look right:
- mesh rendering with legacy materials, textures, per-group flags and animations;
- planet quadtree with DDS tiles, elevation, clouds, night lights and water specular;
- atmosphere scattering and horizon haze;
- star field and celestial sphere;
- exhaust and particle streams;
- HUD/MFD/panel `Sketchpad` (text, lines, polygons, blits);
- 2D panels and the virtual cockpit;
- ImGui;
- custom cameras (the MFD camera addons use them);
- base structures and runway lights.

Nice-to-have for parity with D3D9Client: PBR/metalness, env maps and irradiance, shadow maps, glare/lens flare, bloom, SSAO, the gcCore extension API and debug tools.

---

## 7. Platform-Dependency Inventory (Core)

| Dependency | Where | Replacement |
|---|---|---|
| Win32 windowing, message loop, `HWND` (626 lines / 49 files) | `Orbiter.cpp`, `DlgMgr`, `Launchpad`, `GraphicsAPI.cpp` | SDL3 or winit |
| Launchpad = Win32 dialog resources (105 dialogs in `Orbiter.rc`), common controls, riched20, `htmlctrl.c` | `Launchpad.cpp`, `Tab*.cpp`, `OptionsPages.cpp` | ImGui (in-sim dialogs were already ported to ImGui in PR #614) or egui |
| DirectInput 8 (keyboard, joystick) | `Input.cpp`, `Di7frame.cpp`, `Orbiter.cpp` | SDL3 input/gamepad/joystick, or `gilrs` |
| `LoadLibrary`/`GetProcAddress` | Orbiter, Vessel, Celbody, Script | `dlopen` / `libloading` |
| HTML Help (`.chm`) | `Help.cpp` | Bundled HTML plus system browser |
| WIC image writer, `HBITMAP` | `GraphicsAPI.cpp` | `image` crate or stb |
| Registry (Wine detection) | `Orbiter.cpp:~415` | Remove |
| `CreateThread` | scenario-dir watcher, dialog thread, console | `std::thread` / `notify` |
| `timeBeginPeriod`, `Winmm` | Orbiter ctor | Not needed |
| Paths with `\\` (≈68 string literals in core; more in addons) and case-insensitive names (`stricmp` in 38 files) | everywhere | Normalise separators at the VFS layer; case-insensitive lookup (APFS is case-insensitive by default, Linux is not) |
| `DWORD`/`LONG` widths | public headers | `uint32_t` typedefs in a portable SDK |
| irrKlang (XRSound) | `Sound/XRSound` | irrKlang has a macOS build but is proprietary; prefer miniaudio/OpenAL (C++) or kira/cpal (Rust) |
| Legacy D3D7 types in core (`d3d.h`, `D3DVECTOR`, `LPDIRECT3DDEVICE7` in `Baseobj.cpp`, `Mesh.h`) | core | Plain structs |
| HtmlHelp, MFC, Qt5 (Utils only) | Utils | Out of scope initially |

The upstream Linux preset uses **`winegcc`** (Winelib) and disables D3D9Client (`CMakePresets.json`), so "Linux support" upstream currently means "compile against Wine's Win32".

---

## 8. Prior Art and Community Efforts

| Effort | Approach | Status (as found) |
|---|---|---|
| **SDL3 port** (orbitersim/orbiter PR #561, ThePuzzlemaker) | Replace Win32 windowing and input with SDL3; remap `OAPI_KEY_*` to SDL scancodes; D3D9Client stays Windows-only | Draft; depends on dialog PR #550; author plans a rebuild ([forum thread](https://www.orbiter-forum.com/threads/pr-sdl3-port-for-cross-platform-support.42146/), [PR](https://github.com/orbitersim/orbiter/pull/561)) |
| **TheGondos Linux branch** | GLFW, glm, OpenAL, libsndfile; experimental **OpenGL client**; test ports of NASSP, XR1/XR5, G42, Deepstar | "Playground"; the author says the GL client needs a full rewrite and the tile loader is broken ([forum](https://www.orbiter-forum.com/threads/linux-playground.40476/)) |
| **racerx2/orbiter-linux** | Line-by-line port: Vulkan 1.4 client ported from D3D9Client, Qt 6 UI, PipeWire | Very early (single commit) ([repo](https://github.com/racerx2/orbiter-linux)) |
| **pml76/orbsim** | Clean-room C++23 plus Vulkan, SDL3, headless core, unit-typed frames | Early; physics basics only, renders nothing yet ([repo](https://github.com/pml76/orbsim)) |
| Upstream winegcc preset | Winelib build on Linux | CI job `on-push-linux.yml` |

Lessons:
1. Platform glue (SDL3) is tractable and has been prototyped more than once.
2. **The renderer is where every port stalls.** D3D9Client is large and tightly coupled to D3DX effects.
3. Addon source-portability has been shown to work: TheGondos ported the XR series and NASSP.

---

## 9. macOS-Specific Constraints

- **GPU API:** Metal only. OpenGL is capped at 4.1 and deprecated; Vulkan is available only through MoltenVK. `wgpu`'s Metal backend is production quality.
- **No f64 in shaders.** This is already handled by camera-relative float rendering.
- **BC (DXT) textures are supported** on Apple Silicon Macs (wgpu `TEXTURE_COMPRESSION_BC`), so existing `.dds` and `.tree` assets can be uploaded without transcoding. Intel Macs support BC too.
- **arm64:** no Windows x86/x64 DLLs can load (see §5.3). Everything must be built from source.
- **Code signing and notarization:** loading third-party dylibs needs the `com.apple.security.cs.disable-library-validation` entitlement (and hardened-runtime considerations). Addon installers must also deal with quarantine attributes.
- **App bundle vs CWD-relative data:** Orbiter assumes `./Config`, `./Modules` and so on. A macOS build should resolve a *data root* (for example `~/Library/Application Support/Orbiter` or a user-chosen folder) and never rely on the CWD.
- **Retina/HiDPI:** physical vs logical pixels matter for MFD and panel coordinates and for ImGui scaling.
- **Input:** the Mac keyboard layout lacks a numpad on laptops, and Orbiter's default keymap relies heavily on it. A remappable keymap UI is needed.
- **Zero-code baseline:** CrossOver, Wine or the Game Porting Toolkit can run Orbiter today, but D3D9 translation on macOS is slow and fragile. This is useful only as a stopgap and for running legacy binary addons.

---

## 10. Options Analysis

Effort figures below are **rough engineering-month (EM) ranges for an experienced graphics/sim engineer**. They are a way to compare options, not a commitment.

### Option A: Ship via Wine/CrossOver/GPTK (no rewrite)
- **Pros:** zero engine work; binary addons keep working.
- **Cons:** poor performance and stability of D3D9 over MoltenVK/D3DMetal; the stack is not open or controllable; not native.
- **Effort:** ~1 EM of packaging and testing. **Verdict:** stopgap only.

### Option B: Native C++ port, no Rust
Win32 → SDL3, a Launchpad in ImGui, a new C++ renderer (Vulkan + MoltenVK, bgfx, Diligent, or `wgpu-native` via its C API), and a portable SDK.
- **Pros:** fastest path to a native macOS build; can reuse D3D9Client algorithms directly (LGPL); smallest ecosystem disruption.
- **Cons:** keeps the global-state, non-thread-safe architecture; does not meet the Rust goal.
- **Effort:** platform layer 3–5 EM; renderer to "stock content looks right" 8–14 EM; full parity 18+ EM.

### Option C: Rust renderer first (hybrid), keep the C++ core
Implement `oapi::GraphicsClient` as a thin C++ shim that forwards every callback over a C ABI (generated with `cbindgen`/`cxx`) to a Rust `wgpu` renderer crate.
- **Pros:**
  - Rust starts in the most Windows-bound, most-needs-rewriting component.
  - `wgpu` gives Metal, Vulkan and DX12 from one codebase.
  - The core keeps all double-precision physics, so the renderer contract is small and well defined (§6.1).
  - The D3D9 client remains the reference on Windows for A/B comparison.
- **Cons:**
  - Still requires Phase 0 (de-Win32 the core) to run on macOS.
  - The `Sketchpad` and `SURFHANDLE` model must be served across FFI with many small calls per frame. This needs batching or command buffers on the Rust side.
- **Effort:** bridge 1–2 EM; MVP (meshes, planets with tiles, basic atmosphere, stars, Sketchpad, ImGui, 2D panels) 5–8 EM; parity 12–20 EM.

### Option D: Rust core plus C++ compatibility SDK (source-compatible addons)
The simulation is rewritten in Rust. The core exposes a **versioned C ABI** (opaque handles plus function tables). A **header-only (or small static) C++ SDK** re-implements `VESSEL`/`VESSEL2/3/4`, `oapi*`, `MFD2`, `oapi::Module`, `CELBODY2` and `ATMOSPHERE` as forwarders to the C ABI. Callbacks go the other way through a C trampoline vtable that the SDK fills on the addon side.
- **Pros:**
  - Memory-safe, testable, potentially multi-threaded core.
  - Addons such as NASSP, XR and stock vessels recompile on all three OSes.
  - The ABI is no longer tied to MSVC.
- **Cons:**
  - The API surface is huge (~335 `oapi*` plus ~367 `VESSEL` methods, the MFD/Sketchpad/particle/animation APIs and the Lua bindings), and many semantics are implicit in the C++ code: callback ordering, `s0`/`s1` timing, sub-step force evaluation.
  - In-place mesh editing (`MESHGROUP*`) and `VESSELSTATUS2` layout must be preserved or emulated.
- **Effort:** core parity 12–18 EM; C ABI plus C++ SDK shim 4–6 EM; Lua re-binding 2–3 EM (via `mlua`, or keep the C++ interpreter on the shim).

### Option E: Greenfield Rust engine with a new API (like orbsim)
- **Pros:** cleanest architecture (ECS or data-oriented design, multi-threading, modern API).
- **Cons:**
  - Discards the addon ecosystem and most of the stock vessel code (~44k LOC to rewrite).
  - Months before anything flies.
  - Highest risk of never reaching feature parity: 20 years of edge cases in ground contact, docking and Encke switching.
- **Effort:** 36+ EM to reach the current feature level. **Verdict:** not recommended unless the goal is a *new* simulator.

### Option F: Incremental strangler (recommended). Combine B's platform layer, then C, then D.
The phases are detailed in §12. It keeps something shippable at every step and uses the existing C++ as a test oracle.

### Renderer technology sub-choice
| Choice | macOS | Pros | Cons |
|---|---|---|---|
| **wgpu (Rust)** | Metal (native) | One API for Metal/Vulkan/DX12; WGSL via naga; robust validation; BC support | No f64 (fine); some advanced features (bindless, mesh shaders) limited; the lowest-common-denominator model |
| Bevy | Metal via wgpu | Batteries included (asset loading, PBR) | Its ECS and f32 `Transform` do not fit planet-scale plus a precision-sensitive custom renderer; fighting the engine; churn between versions |
| ash (Vulkan) + MoltenVK | Via translation | Maximum control | Extra translation layer on the priority platform; much more code |
| metal-rs / objc2-metal directly | Native | Best on Mac | Needs a second backend for Linux and Windows |
| C++ libs (bgfx, Diligent, Filament) | Metal | Mature | Not Rust; FFI either way |

**Pick `wgpu`.** For shaders, either rewrite in **WGSL**, or author in **Slang** (HLSL-like, which eases porting the existing `.fx`/`.hlsl` logic) and compile to WGSL, MSL and SPIR-V at build time. **Licensing caveat:** translating Jarmo Nikkanen's LGPL shaders (`Scatter.hlsl` and others) makes the Rust renderer a derivative work under LGPL. For an MIT renderer, implement the atmosphere clean-room, for example Bruneton's precomputed scattering (BSD) or Hillaire 2020, using only the *algorithms and parameters* described publicly.

---

## 11. Proposed Rust Architecture

### 11.1 Workspace layout
```
orbiter-rs/
 ├─ crates/
 │   ├─ orb-math        f64 vectors/matrices/quaternions (glam DVec3/DMat3/DQuat), frames, units
 │   ├─ orb-time        TimeData (MJD, SimT offset+increment), warp, fixed-step
 │   ├─ orb-ephem       VSOP87, ELP82, Lieske E5, SatSat, Kepler elements (pure, no I/O)
 │   ├─ orb-grav        point-mass sources, J-coeffs, Pines SH (EGM96 etc.)
 │   ├─ orb-atmo        J71G, NRLMSISE-00, Mars2006 (port of the Src/Celbody atmospheres)
 │   ├─ orb-phys        RigidBody integrators (RK2..8, SY2..8), Encke, sub-stepping, touchdown, supervessel
 │   ├─ orb-formats     .cfg/.scn/.msh/.tree/.dds/.tab parsers + writers, VFS with case-insensitive lookup
 │   ├─ orb-terrain     ZTree reader, elevation manager (shared by physics and renderer)
 │   ├─ orb-sim         World (bodies, vessels, bases), update scheduler, events, flight recorder
 │   ├─ orb-abi         #[repr(C)] stable C ABI: handles, function tables, versioning
 │   ├─ orb-render      wgpu renderer: meshes, planet quadtree, atmosphere, stars, particles, sketchpad
 │   ├─ orb-ui          ImGui (dear-imgui bindings) + launchpad; HUD/MFD logic (ported from Pane/Mfd*.cpp)
 │   ├─ orb-input       SDL3 or winit + gilrs; keymap
 │   ├─ orb-audio       kira/cpal; XRSound-compatible API surface
 │   ├─ orb-script      mlua (Lua 5.1 compatibility mode) bindings
 │   └─ orbiter         the binary: plugin host (libloading), main loop
 └─ sdk/
     ├─ c/              orbiter_abi.h (generated by cbindgen)
     ├─ cpp/            OrbiterAPI.h / VesselAPI.h / DrawAPI.h … re-implemented over the C ABI
     └─ rust/           orbiter-sdk crate for Rust-native addons
```

### 11.2 Core design choices
- **Keep the two-phase `s0`/`s1` update**, expressed as `prev: &WorldState` plus `next: &mut WorldState`. This removes the implicit ordering hazards, allows **parallel per-vessel integration** (with rayon) once gravity-source reads come only from `prev` and interpolated celestial states, and makes determinism testable.
- **Handles, not pointers.** Use generational arena indices (for example `slotmap`) for bodies, vessels, thrusters, tanks and docks. The C ABI exposes them as opaque `u64`. This matches Orbiter's existing `OBJHANDLE`/`THRUSTER_HANDLE` style.
- **No ECS for the core.** The domain has around 10–1,000 heterogeneous entities with rich per-type behaviour and addon callbacks. Plain structs plus arenas are simpler and more deterministic, and nothing stops the renderer from using ECS-like batching internally.
- **Numerics:** reproduce the C++ operation order at first, to pass golden tests. Parameterise integrator tableaux exactly as `BodyIntegrator.cpp` does. Keep the `rpos_base + rpos_add` compensated-sum trick.
- **Globals → context.** Replace `g_psys`/`g_pOrbiter`/`td` with an explicit `SimContext` passed into callbacks. The C++ compat SDK can hide it behind thread-local "current sim" pointers, so `oapiGetSimTime()` still works.

### 11.3 Plugin ABI design
- One exported symbol per plugin: `orbiter_plugin_entry(host: *const HostApi, ver: u32) -> *const PluginApi`. `HostApi` is a `#[repr(C)]` table of function pointers, grouped by area (sim, vessel, mesh, sketchpad, mfd, ui, fs), each with its own version and size for forward compatibility.
- Vessel classes register a factory. Instances return a `VesselCallbacks` table (`pre_step`, `post_step`, `load_state`, `save_state`, `draw_hud`, `consume_key`, …) plus a `*mut c_void` user pointer.
- The **C++ SDK** provides `class VESSEL4` with the familiar virtuals and generates the trampoline table. Old `ovcInit` code then compiles unchanged. `DLLCLBK ovcInit` becomes a macro-generated `orbiter_plugin_entry`.
- **Deliberate breaking changes** (to document, and to shim where cheap):
  - `HDC`-based APIs map to `Sketchpad`.
  - `DWORD` becomes `uint32_t`.
  - `DLGPROC`/Win32 dialogs map to `ImGuiDialog`.
  - `std::string`/`std::vector` in ABI structs become `const char*` plus length.
  - Path strings are normalised by the host VFS.
- **Optional Windows-only binary bridge:** a C++ DLL that exports the 2016 MSVC-mangled symbols and forwards to the C ABI would let *unmodified binary addons* keep working on Windows x86/x64. This is valuable for the Windows user base, but it is optional and independent of macOS.
- **ImGui:** addons compile against the SDK's ImGui. The host shares `ImGuiContext*` explicitly (`ImGui::SetCurrentContext`) instead of exporting `GImGui`. Pin one ImGui version per SDK major version.

### 11.4 Renderer design (wgpu)
- **Coordinates:** f64 world, **camera-relative f32** per draw (as D3D9Client does). **Reversed-Z with an infinite far plane** removes most of the per-planet near/far partitioning. Keep a two-range fallback for the cockpit versus exterior views.
- **Planets:** a quadtree identical to `TileManager2`, so the same `.tree` archives and tile indices work. Load tiles asynchronously (thread pool) with an upload queue. Upload BC textures directly; decode zlib with `flate2`. Tile meshes can be generated on the CPU (as now) or on the GPU from an elevation texture (vertex pulling), which shrinks memory and upload time.
- **Meshes:** parse `.msh` into GPU buffers. Support `EVENT_VESSEL_*` edits and animations (`clbkAnimate` sets transforms per group), and a legacy material model plus PBR.
- **Sketchpad:** implement it as a retained, batched 2D command list rendered with a small vector/text pipeline. The text uses the glyph-atlas approach from `D3D9TextMgr`, or `glyphon`/`cosmic-text`. Surfaces become render-target textures.
- **Frame graph:** the pass order follows §6.2. Start with: celestial sphere → planets (surface, clouds, atmosphere) → bases → vessels → particles → transparent → post → 2D overlay (HUD/MFD/panels) → ImGui.
- **Shaders:** WGSL or Slang-generated. Validation is via `naga`.

### 11.5 UI, input and audio
- **ImGui** is effectively mandated by the addon API (`ImGuiDialog`, `ImPlot`, `IconsFontAwesome6.h` in the SDK). Use a Rust Dear ImGui binding for the host and the C++ ImGui inside addons, sharing the context through the C ABI. The Launchpad can be rebuilt in ImGui (upstream is already moving in-sim dialogs this way).
- **Input:** SDL3 covers keyboard, mouse, joystick and gamepad with good macOS support, and upstream PR #561 has already defined the `OAPI_KEY` → SDL scancode mapping. `winit` plus `gilrs` is the pure-Rust alternative.
- **Audio:** `kira` or `cpal`, behind an XRSound-compatible API (`Sound/XRSound/src/XRSound.h`), so XR vessels keep sound.

---

## 12. Recommended Roadmap (Option F)

| Phase | Goal | Key work | Exit criteria | Rough effort |
|---|---|---|---|---|
| **0. Portable C++** | Current C++ builds natively on macOS/Linux (graphics stubbed or headless) | Adopt or finish SDL3 (PR #561); ImGui Launchpad; remove DirectInput, WIC, HtmlHelp and registry; `dlopen`; VFS for paths and case; `DWORD` → `uint32_t`; clang build; macOS CI | Headless `Orbiter --scenario X --fixedstep 0.02 --maxframes N` runs on macOS arm64 and produces identical `CurrentState.scn` output to Windows (within tolerance) | 3–5 EM |
| **1. Rust renderer** | Native macOS visuals with stock content | C-ABI graphics bridge; `wgpu` client: meshes, planets and tiles, stars, basic atmosphere, particles, Sketchpad, 2D panels, VC, ImGui | DG-S takeoff, ISS docking and Moon landing scenarios are playable on macOS; screenshot diffs against D3D9Client are reviewed | 6–9 EM (MVP) |
| **2a. Golden-master harness** | Lock down physics behaviour | Scenario corpus plus per-frame state dumps from the C++ core (headless, fixed step) | CI can diff any core against the reference | 1 EM |
| **2b. Rust core** | Replace `Src/Orbiter` sim logic | Port math/time/ephem/grav/atmo → integrators/Encke → vessel forces/touchdown → docking/supervessel/attachments → nav/recorder → HUD/MFD logic | All golden scenarios match (position error below 1 m over N frames, or a documented reason); stock vessels run | 10–16 EM |
| **2c. Compat SDK** | Addons compile on all OSes | C ABI (`cbindgen`); C++ SDK re-implementation; Lua via `mlua` or the kept C++ interpreter | Stock vessels plus TransX plus at least one large community addon (NASSP or XR) build and fly on macOS | 4–7 EM |
| **3. Consolidate** | One Rust binary | Delete the C++ core; optional Windows binary-ABI bridge; Rust addon SDK; multi-threaded updates | Release | ongoing |

Why this order:
- **Phase 0 is required by every option except A**, and it can be upstreamed. It benefits the whole community and reduces fork drift.
- **Phase 1 delivers the macOS priority earliest**, and the renderer is the component that needs rewriting regardless.
- **Phase 2 starts only once there is a test oracle**, which is what makes a faithful physics port feasible.

---

## 13. Testing and Validation Strategy

- **Golden-master physics.** Upstream already supports deterministic headless runs: no graphics client means `ConsoleNG`, together with `--fixedstep`, `--maxframes`/`--maxsimtime` and the fixed `srand(12345)`. Add a debug plugin, or reuse `Scenarios/Tests`, to dump `s0` for every body each K frames. Compare the Rust core with tolerances that grow over time, since chaotic divergence is expected; long-horizon comparisons should use orbital elements rather than positions.
- **Unit tests** for ephemerides against the published VSOP87 and ELP test values, and for Pines gravity against EGM96 reference accelerations. Also test integrator convergence orders and scenario round-trip (read → write → read).
- **Renderer.** Deterministic camera scripts produce screenshot captures with a perceptual diff (FLIP) against D3D9Client on Windows. Add GPU timing budgets per pass.
- **API conformance.** `Scenarios/Tests/VesselApiTest.scn` and `GeneralApiTest.scn` plus `Tests/Lua.Interpreter.cpp` exist today. Extend them and run them against both cores.

---

## 14. Risks and Open Questions

1. **Hidden semantics in the API.** A concrete example: `clbkPostStep` receives `SimT1` and runs *after* `g_psys->Update()` but *before* `FinaliseUpdate()` swaps `s1`→`s0` (`Orbiter.cpp:1951-1970`, `Body.cpp:163-173`). State getters inside `clbkPostStep` therefore still return the *pre-step* vectors. Other examples: sub-step force evaluation, and `oapiMeshGroup` returning live mutable vertex data. *Mitigation:* golden tests plus porting the stock vessels early as canaries.
2. **Numerical drift.** Different floating-point contraction changes trajectories. Clang on arm64 defaults to `-ffp-contract=on`, so it fuses into FMA, while MSVC x64 typically does not. *Mitigation:* build the C++ reference with `-ffp-contract=off` on macOS. Rust never contracts implicitly, but avoid `mul_add` in the ported physics unless the reference does the same. Compare with tolerances.
3. **Scope creep in the renderer.** D3D9Client has over 10 years of features. Define an MVP list (§6.3) and defer PBR, env maps and SSAO.
4. **Licensing.**
   - Core: MIT.
   - D3D9Client: LGPL/GPL. A Rust port of its code or shaders inherits the LGPL.
   - irrKlang: proprietary, so it should be replaced.
   - VSOP87 and ELP82 data are public.
   - Decide early whether the new renderer is clean-room (MIT) or a derivative (LGPL).
5. **Ecosystem buy-in.** Coordinate with upstream maintainers (jarmonik, TheGondos, DougB and the SDL3 PR author), so that Phase 0 lands upstream rather than in a fork.
6. **Binary-only addons.** These are lost on macOS by physics of the platform. Communicate this early and offer the Windows bridge.
7. **Tooling.** `Utils/tileedit` (Qt5) and `meshc` matter to content creators. Port them later or replace them with Rust CLI tools built on `orb-formats`.

---

## 15. Appendix: Key Code Locations

| Topic | Location |
|---|---|
| Entry point | `Src/Orbiter/Orbiter.cpp:170` (`WinMain`) |
| Main loop | `Src/Orbiter/Orbiter.cpp:1015` (`Run`) |
| Time step begin/end | `Src/Orbiter/Orbiter.cpp:1745`, `:1787`; `Src/Orbiter/TimeData.h` |
| World update | `Src/Orbiter/Orbiter.cpp:1951`; `Src/Orbiter/Psys.cpp:815` |
| Session creation | `Src/Orbiter/Orbiter.cpp:691` |
| Plugin loading | `Src/Orbiter/Orbiter.cpp:533-665` |
| Vessel module loading | `Src/Orbiter/Vessel.cpp:5943` (`LoadModule`), `:5970` (`RegisterModule`) |
| VESSEL API forwarders | `Src/Orbiter/Vessel.cpp:6231…` |
| Rigid body update, Encke | `Src/Orbiter/Rigidbody.cpp:182` |
| Integrators | `Src/Orbiter/BodyIntegrator.cpp` |
| Vessel update and touchdown | `Src/Orbiter/Vessel.cpp:4717`, `:4289` |
| Propagator defaults | `Src/Orbiter/Config.cpp:55` |
| Command-line flags | `Src/Orbiter/cmdline.cpp:168-178` |
| Graphics client registration | `Src/Orbiter/GraphicsAPI.cpp:908` |
| 2D overlay hook | `Src/Orbiter/GraphicsAPI.cpp:299` |
| ImGui context export | `Src/Orbiter/DlgMgr.cpp:420` |
| SDK DLL entry | `Src/Orbitersdk/Orbitersdk.cpp` |
| Graphics client interface | `Orbitersdk/include/GraphicsAPI.h:403` |
| Sketchpad | `Orbitersdk/include/DrawAPI.h:978` |
| D3D9 frame | `OVP/D3D9Client/Scene.cpp:1242` |
| Tile manager, loader thread | `OVP/D3D9Client/Tilemgr2.h`, `Tilemgr2.cpp:829` |
| Tile archive format | `OVP/D3D9Client/ZTreeMgr.h`, `Src/Orbiter/ZTreeMgr.h` |
| Mesh format parser | `Src/Orbiter/Mesh.cpp:828` |
| Export ABI check | `exports.2016.txt`, `.github/workflows/reusable-build.yml:116-129` |
| Linux (winegcc) preset | `CMakePresets.json` |

---

### Sources (external)
- [Orbiter Forum: PR SDL3 port for cross-platform support](https://www.orbiter-forum.com/threads/pr-sdl3-port-for-cross-platform-support.42146/)
- [orbitersim/orbiter PR #561](https://github.com/orbitersim/orbiter/pull/561)
- [Orbiter Forum: Linux playground (TheGondos)](https://www.orbiter-forum.com/threads/linux-playground.40476/)
- [racerx2/orbiter-linux (Vulkan / Qt 6 / PipeWire)](https://github.com/racerx2/orbiter-linux)
- [pml76/orbsim (C++23 / Vulkan clean-room)](https://github.com/pml76/orbsim)
