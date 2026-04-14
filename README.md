# OSF Menu Framework

Maybe?

### Requirements
* [XMake](https://xmake.io) [3.0.0+]
* C++23 Compiler (MSVC, Clang-CL)

### Build
To build the project, run the following command:
```bat
xmake build
```

> ***Note:*** *This will generate a `build/windows/` directory in the **project's root directory** with the build output.*

### Build Output (Optional)
If you want to redirect the build output, set one of the following environment variables:

- Path to a Mod Manager mods folder: `XSE_SF_MODS_PATH`

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
