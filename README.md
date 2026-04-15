# OSF Menu Framework

An SFSE plugin scaffold for Starfield with a Dear ImGui-powered D3D12 debug overlay.

### Requirements
* [XMake](https://xmake.io) [3.0.0+]
* C++23 Compiler (MSVC, Clang-CL)

### Clone
This repository now includes vendored submodules for both CommonLibSF and Dear ImGui. After cloning, initialize them with:

```bat
git submodule update --init --recursive
```

### Build
To build the project, run the following command:
```bat
xmake build

xmake project -k compile_commands
```

> ***Note:*** *This will generate a `build/windows/` directory in the **project's root directory** with the build output.*

### Build Output (Optional)
If you want to redirect the build output, set one of the following environment variables:

- Path to a Mod Manager mods folder: `XSE_SF_MODS_PATH`

### Debug Overlay
The plugin now installs a D3D12 Dear ImGui overlay during SFSE startup.

- Press `F10` in-game to open or close the `OSF Debug` overlay.
- The overlay renders an overview panel with plugin/runtime state and can host future registered debug panels.
- The default window also exposes an option to open the built-in Dear ImGui demo window for validation.

The overlay is intentionally overlay-only in v1. It does not register a native Starfield `IMenu`.

### Papyrus API
This plugin exposes one minimal native Papyrus function for triggering an animation graph event on an actor:

```papyrus
bool ok = OSF_Anim_Native.PlayAnimationEvent(Game.GetPlayer(), "YourAnimEventName")
```

The companion Papyrus declaration lives at `scripts/Source/User/OSF_Anim_Native.psc`.

### Upgrading Packages (Optional)
If you want to upgrade the project's dependencies, run the following commands:
```bat
xmake repo --update
xmake require --upgrade
```
