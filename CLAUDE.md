# OSF Menu Framework

SFSE plugin for Starfield that provides a D3D12 ImGui debug overlay. Uses CommonLibSF, Dear ImGui, and XMake build system.

## Build

```
xmake build
```

Build output is automatically copied to `XSE_SF_MODS_PATH` or `XSE_SF_GAME_PATH` if set.

## Ghidra Labels (`ghidra_scripts/starfield_labels.json`)

This file is the central database of all reverse engineering findings for Starfield's renderer (game version 1.16.236.0). It is consumed by `ghidra_scripts/ApplyLabels.java` which applies the labels to a Ghidra project.

**Keep this file up to date** whenever new RE findings are discovered during analysis. Every function rename, global identification, struct field, or comment from Ghidra analysis should be captured here.

### Confidence levels

Every entry in the JSON should include a `"confidence"` field to distinguish between verified facts and educated guesses:

- **`"confirmed"`** — Name or offset verified from error strings, debug strings, RTTI, Win32 API usage patterns, or COM vtable indices. These are ground truth.
  - Example: `"pDXGIFactory"` from error string `"arDeviceProperties.pDXGIFactory->CreateSwapChainForHwnd(...)"`
  - Example: `"CreateDXGIFactory2"` identified by the Win32 API call itself
  - Example: field offset validated by `static_assert(offsetof(...))` in compiled code

- **`"high"`** — Strong inference from multiple sources. The name/purpose is almost certainly correct even if no string directly confirms it.
  - Example: a function that calls `D3D12CreateDevice`, stores the result, and is only called during init — naming it `DeviceProperties_InitD3D12` is high confidence
  - Example: a global accessed in the same pattern as a known singleton, adjacent in memory to confirmed globals

- **`"medium"`** — Reasonable inference from context, call patterns, or structural analysis. Could be wrong about the specific purpose but the general area is right.
  - Example: a function called during shutdown that frees resources — naming it `Renderer_Cleanup` based on context
  - Example: a global pointer used with UI-related functions named `g_UIRenderer`

- **`"low"`** — Speculative. Named based on limited context, position in code, or analogy with FO4's CommonLibF4. May need revision.
  - Example: naming a function `Renderer_GetState_A` when all we know is it's near other renderer functions and returns a value

### JSON schema

```json
{
  "version": "1.16.236.0",
  "structs": [
    {
      "name": "StructName",
      "size": "0xNN",
      "confidence": "confirmed|high|medium|low",
      "comment": "optional note",
      "fields": [
        {
          "offset": "0xNN",
          "type": "pointer|int|uint|bool|byte|...",
          "name": "fieldName",
          "confidence": "confirmed|high|medium|low",
          "comment": "optional"
        }
      ]
    }
  ],
  "globals": [
    {
      "address": "0x14XXXXXXX",
      "name": "g_SomeName",
      "confidence": "confirmed|high|medium|low",
      "comment": "optional"
    }
  ],
  "functions": [
    {
      "address": "0x14XXXXXXX",
      "name": "FunctionName",
      "confidence": "confirmed|high|medium|low",
      "comment": "optional",
      "commentType": "plate|eol|pre|post"
    }
  ]
}
```

### Conventions

- Addresses are hex strings with `0x` prefix
- Struct sizes and field offsets are hex strings
- Function names use PascalCase (e.g. `DeviceProperties_InitD3D12`)
- Global names use `g_` prefix with PascalCase (e.g. `g_RendererRoot`)
- Bethesda-confirmed names preserve their exact casing (e.g. `pDXGIFactory`, `pDxDevice`)
- IID/GUID globals use `IID_` prefix
- The `"commentType"` field on functions defaults to `"plate"` if a comment is present
- The `ApplyLabels.java` script ignores the `confidence` field — it's for human/AI reference only
