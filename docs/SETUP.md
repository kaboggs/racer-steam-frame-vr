# Local setup and rollback

## Requirements

- Steam Frame running ARM64 SteamOS with working native SteamVR.
- Your legal, installed original PC copy of STAR WARS Episode I Racer. Tested: Steam app 808910, stored on an SD card. GOG is supported by upstream but has not been tested in this Frame port.
- Linux aarch64 or x86_64 build host with `git`, CMake 3.16+, a native build tool (Make), Python 3.10+ and the Python `venv` module. Network access is needed for the pinned upstream source, LLVM-MinGW and Python dependencies.
- Sufficient free space for a private upstream checkout, compiler and build (allow several GB). No Windows PC is needed; building on Frame is possible with these prerequisites.

The script downloads LLVM-MinGW from its official release and verifies the pinned SHA256. It downloads a pinned official Valve 32-bit OpenVR loader and verifies its SHA256. Jinja2 is version-pinned and installed in an isolated virtual environment. The upstream commit is pinned. These downloads are local build dependencies, not part of the public repository.

## Build

From the repository root:

```bash
python3 scripts/setup.py build --jobs 4
```

Build output: `dist/payload/` and `dist/manifest.json`. The DLL target is 32-bit Windows, even though Frame and its SteamVR runtime are ARM64. Proton handles the Windows process and VR bridge.

The repository is source-only, so the first build takes time. `.cache/`, `build/` and `dist/` must remain private and ignored. A build does not need access to any game install directory.

## Install

Close Racer first. Use Steam's Manage > Browse local files to find the game folder. The selected folder must contain `SWEP1RCR.EXE`.

```bash
python3 scripts/setup.py install --game-dir "/path/to/Star Wars Episode I Racer" --dry-run
python3 scripts/setup.py install --game-dir "/path/to/Star Wars Episode I Racer"
```

Dry run checks the payload and destination and changes nothing. A real install backs up **only the allowlisted mod files** before overwriting them. The backup path is printed and saved under `~/.local/share/racer-frame-vr/backups/`. No game executable, data directory, saves, videos, models or textures are copied into the backup.

Installed files: `dinput.dll`, `openvr_api.dll`, `assets/racer_openvr_actions.json`, `assets/racer_frame_bindings.json`, and text `.vert`/`.frag` shaders under `assets/shaders/`. These filenames can replace a previous mod, so keep the printed backup path. The installer validates SHA256 values and rejects unexpected payload entries and destination symlinks.

Optional HD fonts and loading-screen images are not installed. This preserves the game's own resources. The previous experimental installation included upstream optional images; they were not required to establish the OpenVR session. A completely clean-install visual test with this source-only payload is still needed.

## Steam properties

For Racer, select **Proton Experimental (ARM64)**. The tested tool identifier was `proton-experimental-arm64` (not `proton_experimental-arm64`). Put this in Launch Options:

```text
WINEDLLOVERRIDES="dinput=n,b;ucrtbase=b;openvr_api=n" PROTON_LOG=1 %command%
```

These settings are manual; the installer does not edit Steam's databases. No Mesa GL/GLSL version override is required. Launch through Steam after SteamVR/headset startup.

## First headset test

Use short A taps to select menus. Left stick navigates; A confirms. Right trigger can also confirm. In a race: left stick steers, right trigger supplies thrust, left trigger brakes, A boosts, right stick controls pitch, grips roll, left stick click changes camera, right stick click slides. These are the intended mappings; only menu navigation/confirmation and entering/playing a race were confirmed. Verify each mapping before calling the controls complete.

Steam may label the Proton process **wine-preloader**. A Steam system/dashboard panel is separate from Racer's input. Short-press the Steam-logo/system button to dismiss that panel. If a floating desktop-game screen remains during a race, try **Return to Dashboard** on its control bar, then hide the dashboard with the Steam button. That hiding sequence is supported by the installed SteamVR UI code but has not yet been confirmed by the tester. Do not close the game as a substitute for hiding the screen.

## Restore

Use the exact backup directory printed by install:

```bash
python3 scripts/setup.py restore --backup "/path/to/backup" --dry-run
python3 scripts/setup.py restore --backup "/path/to/backup"
```

Restore replaces previously existing mod files and removes only files the installer added. It validates all current hashes before making changes and refuses to overwrite later modifications. Keep backups if the game updates or another mod changes those files; restore manually after inspection in that case. Clear the Steam launch options/compatibility override manually if returning to vanilla.

## Troubleshooting/logs

Preserve `hook.log`, game `crashes/*.log`, and the Proton log before another launch; some logs are overwritten. `[Frame transition]` markers identify menu states, cinematic entry/exit and track initialization. New crash reports include register values and fault-adjacent instruction bytes. These logs may contain personal paths, so do not publish them wholesale.

If Steam is still stopping Racer, wait until it is stopped before replacing a DLL. Do not restart SteamVR unnecessarily or interrupt another headset application's test. No automatic crash recovery or unattended launch is implemented.
  
