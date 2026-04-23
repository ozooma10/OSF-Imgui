# OSF Menu Framework

An SFSE plugin for Starfield that hosts a Dear ImGui overlay on the game's D3D12 swap chain and exposes a small C API other SFSE plugins can link against to register their own windows, menu items, HUD elements, and input callbacks.

Think of it as a lightweight, always-available "Mod Control Panel" layer — you write an ImGui render function, call `AddSectionItem(...)` from your own plugin, and it shows up as a tree node in the shared menu. Press **F10** in-game to open it.

## Requirements

- [XMake](https://xmake.io) 3.0.0+
- A C++23 toolchain (MSVC or Clang-CL)
- Starfield 1.16.236.0 with SFSE
- *Optional:* `XSE_SF_MODS_PATH` (Mod Organizer mods folder) or `XSE_SF_GAME_PATH` (game root) set in your environment so post-build copies the plugin, PDB, and `resources/fonts/*.ttf` into the right `SFSE/Plugins/` directory.

## Clone and build

The project vendors CommonLibSF (from the [ozooma10/commonlibsf](https://github.com/ozooma10/commonlibsf) fork) and cimgui+Dear ImGui as submodules.

```bat
git clone https://github.com/<you>/OSF-Menu-Framework.git
cd OSF-Menu-Framework
git submodule update --init --recursive

xmake build
xmake project -k compile_commands
```

To refresh dependencies:

```bat
xmake repo --update
xmake require --upgrade
```

## How it works


| Hook | Purpose |
| --- | --- |
| `SwapChainWrapperHook` | Captures `IDXGISwapChain` + `ID3D12CommandQueue` the first time the engine creates its swap-chain wrapper, then bootstraps the ImGui D3D12 backend. |
| `FramePresentHook` | Wraps the engine's end-of-frame call to render the overlay before the real present. |
| `RawInputQueueHook` | Feeds Win32 raw input into ImGui and swallows it when the overlay wants capture. |
| `InputEventReceiverHook` | Patches `UI::PerformInputProcessing` so overlay-consumed input never reaches the menu/UI layer. The previous `PlayerCamera` hook is intentionally disabled until that receiver path is RE-validated. |

The overlay is overlay-only — it does **not** register a native Starfield `IMenu`. Pausing/input-blocking is handled by the framework's own `WindowManager` ([src/WindowManager.cpp](src/WindowManager.cpp)).

## Plugin API

Other SFSE plugins can `dllimport` these from `SFSEMenuFramework.dll` (see [src/SFSEMenuFramework.h](src/SFSEMenuFramework.h)):

```cpp
void             AddSectionItem(const char* path, RenderFunction render);
WindowInterface* AddWindow(RenderFunction render);
int64_t          RegisterInpoutEvent(InputEventCallback cb);
void             UnregisterInputEvent(uint64_t id);
int64_t          RegisterHudElement(HudElementCallback cb);
void             UnregisterHudElement(uint64_t id);
bool             IsAnyBlockingWindowOpened();
ImTextureID      LoadTexture(const char* path, ImVec2* size);
void             DisposeTexture(const char* path);
void             PushBig(); PushDefault(); PushSmall();
void             PushSolid(); PushRegular(); PushBrands();
void             Pop();
```

`path` is a `/`-separated tree path, e.g. `"MyMod/Tweaks/Camera"`, which populates the left-hand tree view rendered by [src/UI.cpp](src/UI.cpp).

## Layout

```
src/              Plugin source (Hooks, Overlay, WindowManager, UI, Input, HUDManager, ...)
src/RE/           Hand-written RE types not yet in CommonLibSF (e.g. BSGraphics)
lib/commonlibsf/  Submodule — forked CommonLibSF
lib/cimgui/       Submodule — cimgui + vendored Dear ImGui and DX12/Win32 backends
resources/fonts/  TTFs copied into SFSE/Plugins/Fonts/ on install
ghidra_scripts/   Starfield RE label database + ApplyLabels.java (see CLAUDE.md)
tools/dev/        Start-StarfieldDebug.ps1 — build, launch via MO2, attach debugger
offsets-1-16-236-0.txt  Address-library dump used while reversing
```

## Debugging

[tools/dev/Start-StarfieldDebug.ps1](tools/dev/Start-StarfieldDebug.ps1) automates the build → launch-through-MO2 → wait-for-Starfield → attach-debugger loop. Typical use:

```powershell
pwsh tools/dev/Start-StarfieldDebug.ps1 -Mode debug
```

See the script's `param(...)` block for MO2 path, mode, and skip flags.

## Reverse engineering notes

Findings about Starfield's renderer live in [ghidra_scripts/starfield_labels.json](ghidra_scripts/starfield_labels.json) and are applied to a Ghidra project by `ApplyLabels.java`. The schema, confidence levels, and naming conventions are documented in [CLAUDE.md](CLAUDE.md) / [AGENTS.md](AGENTS.md) — keep the JSON in sync with new findings.
