# Distribution review — September 28, 2026

## Scope

The candidate repository contains only source modifications, two handwritten input-binding JSON files, build/install/restore scripts, prose documentation, license notices and audit/test code. It is a fresh export, not a copy of the installed game, Steam tree, private diagnostics or old experimental package. No executable release is proposed.

Excluded: game executables and data, models, textures, fonts, images, logos, screenshots, music, audio, movies, save files, Steam/SteamVR/Proton files, OpenVR loader binaries, toolchains, compiled mod DLLs, archives, private logs and account credentials. A local build fetches dependencies into ignored directories and creates a private mod payload. Those outputs are not publication files.

## Provenance

- `patches/frame-port.patch`: reviewed source changes against the exact upstream commit in dependencies.json. Upstream declares AGPL-3.0; LICENSE and modification notices accompany the patch.
- `sources/vr_openvr.cpp`: adapted archived upstream OpenVR implementation, with new Frame actions and runtime compatibility work, under the same license.
- `sources/vr_focus_probe.cpp`, scripts and bindings: new interoperability/tooling work in this session, under AGPL-3.0.
- Dear ImGui patch contexts retain MIT attribution in licenses/Dear-ImGui.txt. Valve's OpenVR notice is reproduced, although its loader is not included.
- Documentation is newly written and contains no copied game artwork or private transcripts.

## Ownership and open-source license

We claim no ownership of upstream work, the game, or third-party trademarks. All such rights remain with their respective owners. This is an independent community compatibility project; it is not affiliated with or endorsed by the game publishers or Valve. Each user supplies their own purchased game. No game content or Steam installation files are distributed.

All original contributions in this repository, including code, patches, scripts, tests, bindings and documentation, are open source under AGPL-3.0. Existing upstream work retains its applicable copyright and license notices. See [NOTICE.md](../NOTICE.md), [LICENSE](../LICENSE) and [component notices](../licenses/).

## Repeating the technical audit

Run `python3 scripts/audit.py --game-dir /path/to/your/game` before publication. It checks a literal file allowlist, UTF-8 text-only contents, hashes, private-path/credential patterns, exact duplication against installed files, all reachable Git blobs if a repository exists, and a fresh ZIP export assembled from the allowlist. The optional game scan reports hashes and counts locally; it never copies game files. Run `python3 -m unittest discover -s tests` for installer safety checks.
  
