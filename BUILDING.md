# TongueLab - source code and build instructions (version 1.0.1)

Free software under the GNU GPL v3 or later (see `LICENSE`). Copyright (C) 2026 Bolescothegoat. Third-party licences: `THIRD_PARTY_NOTICES.txt`.

An SKSE plugin (C++23) for Skyrim SE 1.5.97 and AE 1.6.x / 1.7.x (one DLL; the engine addresses come from Address Library).

## Layout

| File | What it is |
|---|---|
| `src/main.cpp` | SKSE plugin entry (`SKSEPlugin_Version` for AE, `SKSEPlugin_Query` for SE 1.5.97), logging, event hookup |
| `src/Tongue.h`, `src/Tongue.cpp` | The tongue itself: finding OStim's tongue armors, the open / close decision, the slide, the shape (skin data) edit, the preview tongue, the voice hook |
| `src/Config.h`, `src/Config.cpp` | Settings (`Data/SKSE/Plugins/TongueLab.toml`) |
| `src/Npc.*`, `src/NpcCore.*` | Per-character settings files |
| `src/UI.*` | SKSE Menu Framework pages |
| `src/FaceLabAPI.h` | Interop header for the optional FaceLab integration (found at run time) |
| `patcher/`, `tools/` | The slide-bone patcher: a table of the few bytes that change per tongue (no mesh data), a Python tool and the source of a Windows exe built from the same table |

## Building

Prerequisites: Visual Studio 2022+ (MSVC, C++23), CMake 3.21+, Ninja, and:

1. **CommonLibSSE-NG 15.5.0** - a checkout of <https://github.com/alandtse/CommonLibSSE-NG> at tag `v15.5.0` (commit `8eadded`), by default at `../_libs/CommonLibSSE-NG-15.5.0`
   (override with `-DCOMMONLIB_DIR=...`). Not the older CharmedBaryon repository: it cannot read the AE 1.7 Address Library files.
2. **SKSEMenuFramework.h** - not included here. Download `resources/SKSEMenuFramework.h` from <https://github.com/QTR-Modding/SKSE-Menu-Framework-3> and put it in `src/`.
3. The packages (`spdlog`, `fmt`, `tomlplusplus`, `rapidcsv`, `directxtk`, `directxmath`, `xbyak`) built for the `x64-windows-static-md` triplet, by default in
   `deps15/x64-windows-static-md` (override with `-DDEPS_ROOT=...`); `vcpkg.json` lists them (`vcpkg install --triplet x64-windows-static-md --x-install-root=deps15`).

```
cmake --preset release
cmake --build --preset release
```

The DLL is written to `dist/SKSE/Plugins/TongueLab.dll`. At run time it needs the SKSE Menu Framework DLL (for the menu) and the Address Library for your game version.

## Notes for hackers

* **Finding the tongue:** OStim's `Data/SKSE/Plugins/OStim/equip objects/*.json` entries of type `tongue` give the armor forms; the worn tongue is `biped->objects[slot].partClone` for those forms. Showing and hiding is `SetAppCulled`, so OStim's own equip state stays right.
* **The slide:** the tongue meshes of Halo's pack are bound to the head bone. The patcher re-binds the head-weighted vertices to the first bone of the physics chain (`tong1 1`); TongueLab then moves that node's local translation. While the menu has the game paused the engine does not refresh the scene graph, so the Tuning page pushes the node through `UpdateDownwardPass` itself.
* **The shape:** the Visual sliders rewrite each bone's `skinToBone` in the tongue's `NiSkinData` (a whole-tongue transform composed into the bind pose). The layout is validated by probing before anything is written; the original values are kept, so the sliders are absolute.
* **Voice hook:** at start TongueLab walks the loaded modules and keeps every export named `TongueLab_SpeechKind`; the biggest answer counts.
