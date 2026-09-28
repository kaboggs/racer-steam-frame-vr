# Attribution and distribution

This is an unofficial experimental compatibility port, not an Aspyr, Valve, Lucasfilm or Disney product. Buy and install your own legal copy of the original PC game. No game executable, data, textures, models, audio, videos, save files, screenshots or installed-game assets are distributed here.

The backend and source patch derive from [GameOrDie007/Star-Wars-Episode-I-Racer-PCVR](https://github.com/GameOrDie007/Star-Wars-Episode-I-Racer-PCVR) and its archived `dinput_hook/vr_probe_openvr.cpp.bak` OpenVR backend at the pinned commit, built on [tim-tim707/SW_RACER_RE](https://github.com/tim-tim707/SW_RACER_RE). Preserve their attribution and AGPL-3.0 license. See LICENSE. New port code is distributed under the same license. Individual patched bundled components retain their upstream licenses (including Dear ImGui's MIT license); fetching upstream preserves these notices.

Valve's OpenVR SDK/loader has a separate permissive license, reproduced in licenses/Valve-OpenVR.txt. Its 32-bit loader is downloaded from a pinned official source during a local build, not committed here. LLVM-MinGW and Python build dependencies remain external, with their own licenses.

The upstream checkout may contain optional replacement artwork. It is private build-cache material, never copied into this public repository or the generated mod payload. Only upstream text shaders are included in the local payload.

Port modifications were made September 27–28, 2026: OpenVR backend adaptation, Frame input, OpenGL 4.3 compatibility, stock-menu routing, diagnostics and build portability. The patch records changed upstream files; the replacement backend is supplied separately. Original upstream notices remain in the pinned checkout. Dear ImGui's notices are also reproduced in licenses/Dear-ImGui.txt.

## Ownership and licensing disclaimer

All upstream code, game content, trademarks and other third-party work remain the property of their respective owners. This project claims no ownership of that work and is not affiliated with or endorsed by Aspyr, Valve, Lucasfilm or Disney. Users must provide their own purchased game installation.

All original contributions in this repository—including source modifications, scripts, tests, controller bindings and documentation—are released as open source under the GNU Affero General Public License, version 3 (AGPL-3.0). Upstream components retain their own copyright notices and licenses. See LICENSE and licenses/.
  
