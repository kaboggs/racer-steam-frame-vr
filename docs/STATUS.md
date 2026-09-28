# Status of the publication candidate

## Verified on hardware

- Native local Steam Frame execution with the Steam game on SD storage.
- OpenVR scene initialization, head-pose updates, complete eye framebuffers and no recorded Submit error in the tested run.
- Visible VR space and menus, left-stick navigation and A confirmation.
- Track selection, entry into a race and user-reported racing after returning stock tracks to the game's original menu routines.
- The temporary hologram bypass is reverted. Artwork preservation is a project requirement.

## Still needs testing

- Hide the floating SteamVR desktop-game screen while keeping the pod view.
- Explicitly check steering, thrust, brake, boost, pitch, roll, camera, slide, pause/resume and menu return.
- Stereo comfort, HUD alignment/readability, artwork appearance and sustained race stability.
- A clean game installation using only the generated minimal payload (without prior optional font/loading-screen files).
- Custom tracks, GOG, other headsets/runtimes and future SteamOS/Proton versions.

## Port limitations

The adapted older OpenVR backend does not implement newer wheel configuration, haptics, world-space flare settings, pitch smoothing, or the OpenXR quad-layer HUD. Optional renderer APIs return neutral defaults. Menus/HUD use the archived stereo fallback; seat offsets are neutral pending cockpit tuning. These omissions are not evidence that those features work. This repository preserves original game artwork rather than dropping draws to hide faults.

Reference working DLL hash from the local September 27 test:
`916ca737818154a3c33dfefec5777e4bdaa501863d3714c6bf579d677d047da5`.
The source in this repository matches that build's source changes. A fresh build can differ in hash because tool/link timestamps and environment differ; verify behavior, not only this reference hash.

## Next session

1. Identify/dock the floating screen and confirm unobstructed VR racing.
2. Check the controller mapping and a clean return from the race.
3. Verify hologram/preview artwork and a clean-install payload.
4. Trace any failure with transition and register diagnostics; preserve logs privately.
  
