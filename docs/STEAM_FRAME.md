# Steam Frame compatibility investigation

Test date: September 27, 2026. Environment: ARM64 SteamOS 0.4.1 VR variant, Adreno 750, native SteamVR; original PC Steam game on SD storage. Tested Proton Experimental ARM64 build: `experimental-11.0-20260924-cache-arm64`. Results describe that environment, not every future Frame/Proton release.

## Why the official OpenXR build failed

The official v1.5 mod requests a Windows OpenGL graphics binding through OpenXR. On the installed Proton ARM64 bridge it produced:

```text
xrCreateReferenceSpace failed XR_ERROR_HANDLE_INVALID
Unhandled graphics binding type: 1000023000
```

That type is `XrGraphicsBindingOpenGLWin32KHR`. The bridge did not support it. A 32-bit OpenXR runtime/bridge **was present** and runtime registration succeeded; this was not simply a missing 32-bit runtime. Registry changes and forcing the loader did not resolve the graphics binding.

The original renderer asked for OpenGL/GLSL 4.5 while Frame reported a 4.3 ceiling. Version spoofing allowed startup but did not solve OpenXR. An earlier trial also encountered a Wine audio hang. The tested launch settings retain builtin `ucrtbase`; its exact independent effect on the final port has not been isolated.

## OpenVR source port

The upstream archive contained an older OpenVR backend. It was adapted to the current renderer and paired with a 32-bit Valve OpenVR loader. The scene session, per-eye framebuffer creation, head poses and OpenGL texture submission initialized. Headset testing later confirmed VR space and entry into a race.

The context now requests OpenGL 4.3, and an inline material shader changed from GLSL 4.50 to 4.30. This avoids driver-version spoofing. LLVM-MinGW cross-build fixes remove `-mfpmath=387`, correct case-sensitive Windows headers/library names, and add a missing standard-library include.

## Frame controllers and focus

Legacy OpenVR controller state was unreliable. An explicit IVRInput action manifest and `frame_controller` binding were added. SteamVR loaded the bindings and logged the right-hand A press (`0x80`). The input poll now actually runs during frame processing.

Front-end widgets receive virtual-key events, while the hub/race paths consume DirectInput held states and rising edges. Both paths are bridged. Originally, menu key-down and key-up were sent in the same call; release now follows the physical button release. Confirm/Back do not auto-repeat; directional navigation still does. Right-trigger menu confirm no longer also activates boost in a race.

Dashboard focus matters: when Steam's dashboard is open, game action data can become inactive. A read-only background probe can report dashboard visibility and the scene process. Its optional source is `sources/vr_focus_probe.cpp`; compile against the fetched OpenVR header and `libdl` on a system with a native C++ compiler. It does not take scene focus or hide overlays.

## Menu artwork crashes and the successful path

The fork's rewritten course-information menu null-read twice at `004567B9`, reached through DrawHoloPlanet. A temporary experiment bypassed that hologram; the next failure was a null read at `00456CA6` in DrawTrackPreview. The bypass did not fix the underlying issue and was **fully reverted**.

Logs showed returning between the track-select and main-menu states before failing; no race-load entry had occurred. Built-in tracks now use the game's original main, course-selection and course-information routines. This preserves the original artwork/resource lifecycle rather than removing artwork. Custom-track menus retain the fork's extended implementation.

With the original stock-menu path, the user successfully selected a track, entered a match and raced. The log confirmed track-init entry and return. This points to the rewritten menu/resource path, but the exact invalid pointer/lifetime defect has not been independently proved. Avoid overstating this as a fully understood upstream bug.

## Floating desktop screen

A small SteamVR screen remained visible while racing. The installed gamescope session uses a desktop-game overlay, and the installed SteamVR UI has a Return to Dashboard control that docks a floating screen. The recommended local test is to dock it, then hide the dashboard. The tester has not yet confirmed this removes the unwanted screen. Distinguish that overlay from Racer's HUD before changing any rendering code. Do not suppress HUD/artwork as an overlay workaround.

## Sources

- [PCVR fork](https://github.com/GameOrDie007/Star-Wars-Episode-I-Racer-PCVR)
- [Original reverse-engineering project](https://github.com/tim-tim707/SW_RACER_RE)
- [Valve OpenVR](https://github.com/ValveSoftware/openvr)
- [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw)
- [Steam game](https://store.steampowered.com/app/808910/STAR_WARS_Episode_I_Racer/)
- [Aspyr console release](https://www.aspyr.com/games/star-wars-episode-i-racer)
  
