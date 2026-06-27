# Floating Subtitles

SKSE plugin that adds floating subtitles over NPCs
[SSE/AE](https://www.nexusmods.com/skyrimspecialedition/mods/154424)
[VR](https://www.nexusmods.com/skyrimspecialedition/mods/183714)

## Requirements

- [CMake](https://cmake.org/)
  - Add this to your `PATH`
- [PowerShell](https://github.com/PowerShell/PowerShell/releases/latest)
- [Vcpkg](https://github.com/microsoft/vcpkg)
  - Add the environment variable `VCPKG_ROOT` with the value as the path to the folder containing vcpkg
- [Visual Studio Community 2022](https://visualstudio.microsoft.com/)
  - Desktop development with C++
- [CommonLibVR (NG)](https://github.com/alandtse/CommonLibVR/tree/ng)
  - Bundled as the `extern/CommonLibVR` submodule and used for **all** targets
    (SE/AE single-runtime and VR multiruntime); run `git submodule update --init`.
  - The NG library is required because the source uses its runtime accessors
    (`GetRuntimeData`/`GetVRRuntimeData`/`VariantOffset`).

## User Requirements

- [Address Library for SKSE](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
  - Needed for SSE/AE
- [VR Address Library for SKSEVR](https://www.nexusmods.com/skyrimspecialedition/mods/58101)
  - Needed for VR
- [ImGui VR Helper](https://www.nexusmods.com/skyrimspecialedition/mods/183466)
  - Needed for VR (provides the in-world HUD layer)

## Register Visual Studio as a Generator

- Open `x64 Native Tools Command Prompt`
- Run `cmake`
- Close the cmd window

## Building

```
git clone https://github.com/alandtse/FloatingSubtitles.git
cd FloatingSubtitles
git submodule update --init --recursive
```

This produces a single universal DLL that runtime-detects SE, AE, and VR. The
post-build step copies it to every `Skyrim64Path` / `SkyrimAEPath` / `SkyrimVRPath`
you have set.

```
cmake --preset vs2022
cmake --build build --config Release
```

Use `--preset vs2026` instead for the Visual Studio 2026 toolset.

## Licensing

[GPL-3.0-or-later](COPYING) WITH a [Modding Exception and a GPL-3.0 Linking
Exception (with Corresponding Source)](EXCEPTIONS.md), where:

- **Modded Code** — Skyrim and its variants
- **Modding Libraries** — [SKSE](https://skse.silverlock.org/), CommonLib and variants

This is a VR fork of [powerof3's Floating Subtitles](https://github.com/powerof3/FloatingSubtitles);
the original work is © powerofthree under the MIT License.
