# Technical audit — September 28, 2026

Candidate publication set: 20 explicitly allowlisted UTF-8 text files. No game or Steam files, binaries, archives, media, fonts, private logs, saves or credentials are included. Build caches and outputs are excluded. The old private experimental archive is not a publication source.

1. Every publication file was inventoried and its contents/provenance reviewed. AGPL-3.0, Dear ImGui MIT and Valve OpenVR notices are included. Source changes are accompanied by their date and upstream commit.
2. Publication hashes were compared with the local installed game's file inventory. The only exact matches are the two controller-binding JSON files authored and installed during this work; those are explicitly checked against the candidate hashes. No original game or Steam file duplicates were found. Original game assets are never exported. The source patch and replacement backend were compared with the working local source changes.
3. A new export was reconstructed from the literal allowlist and each member was checked against its source hash. All reachable blobs in the fresh local repository were scanned, with no old installation history included.

Nine temporary-file installer tests cover dry run, backup/restore, preserving later user changes, payload tampering, extra executable rejection, symlink/path escape rejection and stopping before an overwrite when a temporary file conflicts. They do not launch the game or modify the live installation.

A fresh configure/build of the pinned upstream with this patch succeeds on the Frame host. The generated local payload is ignored and not part of the public export. A live-install dry run is read-only. This does not establish clean-install runtime behavior; see STATUS.md.

The technical checks are reproducible with scripts/audit.py and tests/test_setup.py. Ownership and licensing are documented in NOTICE.md and DISTRIBUTION.md.
  
