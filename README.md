# Racer Steam Frame VR (experimental)

Run the **original Windows PC version of STAR WARS Episode I Racer** locally on Steam Frame, using an experimental OpenVR port of [GameOrDie007's PCVR mod](https://github.com/GameOrDie007/Star-Wars-Episode-I-Racer-PCVR). Games can remain on an SD card. No PC streaming is required for the tested setup.

**A track was selected and a race was entered and played on Frame on September 27, 2026.** This is an early compatibility milestone, not a fully validated release. A floating SteamVR desktop-game screen remained visible; hiding it and checking full controller coverage, stereo comfort and longer-term stability are still pending. The official Windows/OpenXR mod has different compatibility requirements.

This repository contains source changes, bindings, scripts and documentation only. **It contains no game installation assets or game executables.** You must own and install the game yourself. The tested Steam version is [app 808910](https://store.steampowered.com/app/808910/STAR_WARS_Episode_I_Racer/). Aspyr's newer console rereleases are not supported.

## Start here

- [Setup and rollback](docs/SETUP.md): local build, preview installation, backups and Steam settings.
- [Steam Frame investigation](docs/STEAM_FRAME.md): runtime/graphics/input problems and the fixes that led to racing.
- [Status and next checks](docs/STATUS.md): precisely what has and has not been verified.
- [Distribution and licensing](docs/DISTRIBUTION.md): source-only contents, ownership disclaimer and audit instructions.
- [Attribution](NOTICE.md), [AGPL-3.0 license](LICENSE), [Valve OpenVR license](licenses/Valve-OpenVR.txt).

```bash
python3 scripts/setup.py build
python3 scripts/setup.py install --game-dir "/path/to/Star Wars Episode I Racer" --dry-run
python3 scripts/setup.py install --game-dir "/path/to/Star Wars Episode I Racer"
```

The script does not launch the game, change Steam settings or modify saves. Configure Steam as described in the setup guide. Close Racer before installation. Build prerequisites must already be available; the script does not unlock SteamOS or install system packages.

## Source layout

The upstream commit and external download hashes are pinned in `dependencies.json`. `sources/vr_openvr.cpp` replaces the upstream `dinput_hook/vr_openxr.cpp`; `patches/frame-port.patch` contains the remaining changes. A private ignored cache holds upstream and the compiler. Optional upstream textures/fonts/loading artwork are neither committed here nor copied into the generated install payload. Local builds copy only shader text plus the mod DLL, Valve loader and bindings.

Original game artwork, holograms and features should be preserved. A failed temporary hologram bypass was fully reverted. Built-in tracks use the game's original menus to preserve their resource lifecycle; custom tracks retain the fork's menu path, which is untested on Frame. Optional features absent from this OpenVR backend are documented in the status page rather than presented as working.

## Open source and ownership

All original contributions here are open source under AGPL-3.0, including the scripts, bindings and documentation. We claim no ownership of upstream work or game content. Third-party rights and component licenses remain with their respective owners. See [NOTICE.md](NOTICE.md).

## Thank you

Thank you to **[GameOrDie007 and the PCVR contributors](https://github.com/GameOrDie007/Star-Wars-Episode-I-Racer-PCVR)** for the VR mod and archived OpenVR backend that made this Frame adaptation possible, and to **[tim-tim707 and the SW_RACER_RE contributors](https://github.com/tim-tim707/SW_RACER_RE)** for the reverse-engineering foundation. This project builds on their work and preserves their attribution and licenses.

Thanks also to the maintainers of **[Valve OpenVR](https://github.com/ValveSoftware/openvr)**, **[LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw)**, **[Dear ImGui](https://github.com/ocornut/imgui)** and the other upstream dependencies for the tools and libraries used by this port.
