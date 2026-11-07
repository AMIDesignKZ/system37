# System 37 development status — 7 November 2026

Branch reviewed: `extended-firmware`  
Reviewed HEAD: `a1aa47ba7d9d3f242d25a8d9bec7da2c8517e7f1`  
Rebrand baseline: `bdfd8a7ccee04cafa0258cf04f0a775b4b5b2bb2`  
The branch is 72 commits ahead of that baseline and not behind it.

This note consolidates the work from the 6 November SMK-37 session and the subsequent System 37 expansion. The earlier conversation title “SMK37 USB Detection Issues” is outdated: the USB detection issue was resolved and is not an open task.

## 1. Work completed earlier in the session

- Resolved the SMK-37 USB detection issue.
- Established a successful SMK-37 build on the pre-expansion baseline using `BOARD=smk37 bash build.sh`.
- That successful build predates the six-track/eight-section changes. It is only a baseline result, not a build verification of the current branch.
- The host test work was not a clean full pass: portability/sanitizer issues were encountered and one ASan/UBSan stress stage stopped before the full suite completed. Do not report the complete host suite as passing.
- System 37 rebrand work updated build/package naming, installer/launcher entry points, README/build/test documentation and Windows build scripts. The SMK hardware identity `SMK-37 Pro_900` and upstream Felucca identity/attribution were retained.
- The current repository is `AMIDesignKZ/system37`; the active development branch is `extended-firmware`.

## 2. System 37 target agreed

- Six total tracks: five independently sequenced synth tracks (each Analog or FM6) plus dedicated drum track 6.
- Eight independently stored sections A–H. Each is a complete six-track snapshot.
- 32 arrangement positions, each selecting A–H and holding 1–64 bars.
- Keep the four physical faders and four-column display layout; bank/scroll the visible controls so all six tracks remain accessible.
- Expose the full system in firmware, device UI, SysEx/editor protocol, web editor, backup/restore and emulator package.
- Preserve FUN1–FUN6 imports and existing drum patterns/kit/tune/decay data.

The detailed target remains in `docs/SYSTEM37-EXTENDED.md`.

## 3. Source work now present on the branch

### Project format and migration

- FUN7 format is defined for six tracks, with five synth parts and drum track 6.
- FUN6 remains a frozen legacy import format.
- Legacy drum track 4 (index 3) is moved to track 6 (index 5); new synth tracks 4 and 5 (indices 3 and 4) receive safe empty defaults.
- FUN7 retains 24 parameter locks per track.
- Six-bit signed microtiming is packed into 48 bytes per track and two-bit fill conditions are retained.
- FUN7 track record is asserted at 881 bytes; full project at 5,366 bytes.
- Host-testable FUN7 capture/apply adapter exists in `project_v7_live.c/.h`.
- Legacy drum tune/decay data is carried through the FUN7 drum record.

### Section catalogue and flash

- A/B multi-sector storage primitives exist in `storage_multi.c/.h`.
- A–H catalogue includes eight section records plus a working-project/autosave record.
- Catalogue payload is 48,378 bytes. Each 12-sector copy has 48,896 bytes of payload capacity, leaving 518 bytes.
- The map uses reserved free sectors plus the old project/autosave sectors for the second copy. The existing design preserves the legacy copy until the first new catalogue copy is committed and verified; later A/B rotation can reclaim the old sectors.
- The sample slots, FM6 bank, user preset banks, DAW settings, calibration/settings objects and stock-protected ranges are intended to remain untouched.
- Boot, legacy import, section load/store/erase, working autosave, restore and flush paths have been connected in `project.c`.
- PER3 four-section settings migration to PER4 32-position settings has been added.

### Runtime, controls and MIDI

- SMK-37 runtime constants now select `NPART=5`, `NTRK=6`, `TRK_DRUM=5`; FM-1 remains at three synth parts plus drums.
- Safe defaults were added for synth tracks 4 and 5.
- Track palette and drum colour indexing were extended.
- Mixer and studio track views use four visible rows with scrolling/banking.
- SMK-37 layer keys were extended for six mute/six solo states and A–H section play/store.
- Arranger capacity is 32 positions and eight sections on SMK-37; FM-1 remains 16 positions/four sections.
- MIDI routing now has separate channel IDs for five synth tracks. CH4/CH5 are persisted in the SMK-37 settings object (`OBJ_SMKSET`), not in the legacy MIDI settings byte block.

### Firmware editor protocol and tests

- Firmware track masks, solo/record masks and live-state fields were expanded for six tracks/eight sections.
- Backup object IDs 12–15 are used for sections E–H.
- FUN7 migration, live capture/apply, section catalogue and multi-sector storage host tests have been added or updated.
- The latest commit keeps the FM-1 song controls at their original four-section behaviour while retaining the System 37 A–H target.

## 4. Audit findings — implementation status

The nine findings below were raised against HEAD `0723b59`. Findings 1–8 have now been addressed in source, editor, protocol documentation and regression coverage. They still require a clean target build and complete test run before they can be considered verified.

1. **Section-store call has the wrong argument order.** **[RESOLVED]**  
   In `firmware/src/project.c`, `s37_catalog_store_section()` calls `s37_cat_prepare_overlay()` with the section index where the replacement-project pointer belongs, and the project pointer in the final `default_section` position. The declaration in `section_catalog.h` expects `scratch, replacement, section, bars, default_section`. This will make the section-save path invalid and produces incompatible argument diagnostics.

2. **Arranger readiness checks the legacy format.** **[RESOLVED]**  
   `firmware/src/arranger_scene.c` uses `proj_ok()` in both `arrangement_ready()` and `section_bars()`. On System 37, `project_t` is FUN7, while `proj_ok()` validates a FUN6 image. Consequently FUN7 sections are not marked ready and the arranger will reject otherwise valid arrangements. These checks need the format-aware `project_valid()`.

3. **FUN1–FUN5 import can overwrite its own source buffer.** **[RESOLVED]**  
   In the SMK-37 paths in `project.c`, `proj_fetch()` and `autosave_resume()` call `proj_import(&proj_tmp.v6, &proj_tmp, n)`. The input and output overlap in the same union. FUN6's same-layout copy is harmless, but older conversion routines clear the destination before reading the source, corrupting FUN1–FUN5 input. Use a separate legacy-import staging object.

4. **Backup settings validation still assumes A–D.** **[RESOLVED]**  
   `settings_restore()` calls `arr_valid(&p.arrangement, 15u)`. This rejects valid System 37 arrangements that reference E–H. The old PER3 migration checks using mask 15 are intentional; the current PER4 backup-restore validation is not.

5. **Web LIVE parser still consumes four track records.** **[RESOLVED]**  
   Firmware `editor_live.c` emits one three-byte state record for every `NTRK` track (six on System 37). `web/editor.html`'s `CMD.LIVE` parser still loops exactly four times. It will misalign the rest of the LIVE reply on System 37.

6. **Web backup/restore does not accept the new E–H objects or FUN7.** **[RESOLVED]**  
   Firmware lists backup objects 12–15 for E–H. The web editor's `BK.RESTORE` list and `backupObjects()` whitelist still omit IDs 12–15. Its project-format detection also stops at FUN6 and its device-format check treats FUN7 as newer than the current device. A System 37 backup therefore cannot be reliably restored by the current web editor.

7. **Web Song page and protocol documentation remain old.** **[RESOLVED]**  
   `web/editor.html` and `web/EDITOR_PROTOCOL.md` still describe A–D and the 16-entry PER3 song order. The LIVE section UI is still four sections. The web editor has not yet been brought into line with the 8-section/32-position firmware target.

8. **Hardware MIDI channel controls for synth tracks 4 and 5 are not on a GLO page.** **[RESOLVED]**  
   `G_CH4` and `G_CH5` descriptors and persistence exist, but the `MIDI`, `MIDI 2` and `MIDI 3` page definitions do not expose them. They are available to protocol-level code but not through the current hardware parameter pages.

9. **Performance and final validation are outstanding.** **[OPEN]**  
   The shared voice/CPU/RAM budget has not been profiled for five synth parts. The host test suite, target budget check, SMK-37 package build, emulator and hardware have not been validated against this six-track/eight-section HEAD. The host test labels and target budget baseline still include old four-track assumptions.

## 5. Current status and safe next sequence

The branch contains substantial implementation work, but it is not release-ready and should not be flashed or treated as emulator-ready.

Recommended order:
1. Fix the section-catalogue call signature, FUN7 arranger validity checks, legacy-import staging alias and PER4 backup validation.
2. Audit all fixed track/section assumptions in firmware and test code; expose CH4/CH5 on the hardware UI.
3. Finish the web editor: six-track LIVE parsing/mixer, A–H controls, 32-entry song order, FUN7/E–H backup/restore and protocol documentation/tests.
4. Run the full host suite and resolve sanitizer/portability failures.
5. Build the SMK-37 package, check RAM/flash/CPU budgets, then test in the emulator before hardware.

Do not confuse the earlier successful pre-expansion SMK-37 build with validation of this branch.


## 6. Implementation pass — 7 November 2026

Changes committed on `extended-firmware` after the audit:

- `ba740e2`: corrected section-catalogue overlay argument order and separated legacy FUN1–FUN5 import staging from the raw source buffer in all affected SMK-37 paths.
- `5cf60a2`: switched arranger section validity checks to FUN7-aware `project_valid()`.
- `fc5fbb8` / `54e76e9`: expanded MIDI channel uniqueness to CH1–CH5 plus DRUMS and added a System 37 GLO page exposing CH4/CH5.
- `bd759c3`: made LIVE parsing use the reported track count and added E–H/FUN7 backup handling.
- `76ca536` / `85e7cdd` / `6859446`: extended the web editor to A–H, eight project slots, six LIVE tracks, and PER4's 32-position song order; added PER3-to-PER4 editor-side migration.
- `6743bbe`: documented System 37 protocol extensions.
- `c4c7b9e`: added web regression coverage for PER4 and six-track format detection.
- `709f18e` / `dc7bbc6`: added a board-specific hardware MIDI-page regression test and included it in `tests/run_tests.sh`.
- `a1aa47b`: added `.github/workflows/system37-build.yml` to build Pro/Elite/MKE packages, build the FM-1 reference and run the full host/sanitizer/web test suite, publishing packages as an artifact.

The browser editor now chooses PER3/PER4 from the connected device's reported track count. PER3 orders remain 16 positions/four sections; PER4 orders use 32 positions/eight sections. Song files record track count and include E–H objects when available. Importing a 36-byte PER3 order to System 37 expands it to 68 bytes and moves the legacy trailing lights word to the PER4 offset.

## 7. Validation still required

No current post-audit build or test run has yet been observed. The previous successful SMK-37 build remains a pre-expansion baseline only. The next verification must be:

1. `tools/build_pro.sh` — System 37 Pro, Elite and MKE packages, generated site, then FM-1 reference build.
2. `tests/run_tests.sh` — includes the FUN7/catalogue tests, new System 37 MIDI UI regression, ASan+UBSan editor fuzzing, ThreadSanitizer two-core modes, target-budget checks and web tests.
3. Inspect build output for text/data/BSS/pool headroom and target CPU costs; do not update golden/budget baselines unless an intentional audio or cost change is demonstrated.
4. Validate the generated site/emulator, then test section A–H store/load, 32-position arrangements, MIDI CH4/CH5 and backup/restore on the SMK-37 hardware.

The CI workflow has been added on this development branch. It must produce a successful run before claiming the expanded firmware is build-verified. Do not flash the keyboard before those checks.


## 8. Pro-only build and DX7 ROM integration — 7 November 2026

The build workflow is intentionally scoped to **SMK-37 Pro only** while the first System 37 firmware is brought to a verified state. Elite, MKE and FM-1 reference builds are deferred until the Pro firmware is stable.

### Build evidence

- The Pro target build and package-generation step completed successfully in Actions run `42348468647` (commit `bade3bea`). This is a real post-expansion target build, not the older pre-expansion baseline.
- The generated Pro image was 572,180 bytes; the `.fwsc` package was 651,224 bytes.
- Package validation passed for the `SMK-37 Pro_900` identity, CRCs, stock flash-region layout and loader-compatible package structure.
- Linker budget checks passed: application RAM 98,188 / 98,304 bytes; LOWRAM 28,384 / 29,240 bytes; POOL 335,340 / 344,064 bytes.
- The host/sanitizer/target-budget/web suite did not pass on that run. The immediate compile failure was missing freestanding/host declarations for `memset` and `memcpy` in `section_catalog.c`. Conditional declarations matching the existing `storage_multi.c` approach were added in commit `db178128`; the next run is in progress.
- A follow-up memory-layout review put the eighth section cache in NOINIT and restored a separate catalogue scratch union shared only with legacy-import staging. This is safer than borrowing a live section cache for validation. The change is in commit `3dfa7971`; its build and budget check must be observed before calling the revised layout verified.

### DX7 factory ROM banks

- Added the two user-supplied Yamaha DX7 32-voice SysEx banks as gzip-compressed source assets: `assets/dx7/rom1a.syx.gz` and `assets/dx7/rom1b.syx.gz`.
- The build generator validates the 4,104-byte bulk-dump framing, seven-bit payload and Yamaha checksum before packing all 64 voices into the FM6-compatible 128-byte voice format.
- FM6 now has 72 read-only factory voices: eight original System 37 sounds plus 32 voices from each DX7 ROM.
- The existing 27 writable user-bank slots remain intact. Persistent PTCH values deliberately retain their legacy mapping: F1–F8 = 0–7, B1–B27 = 8–34, DX7 ROM voices = 35–98. The editor library displays factory sounds first and the writable bank after them.
- `tests/dx7_rom_test.py` verifies both ROM dumps and the generated factory table. The ROMs are source inputs; the generated packed voice table is compiled into the firmware.
- Supplied System 37 vector icon and wordmark are in `assets/system37/`. The PNG variants and splash image from the upload have been inspected but are not yet committed; only the SVG icon and logo are currently in the repository.

### Immediate next steps

1. Confirm the current Pro build succeeds with the revised NOINIT scratch allocation.
2. Confirm the full host, sanitizer, target-budget and web suite passes; fix each real regression rather than weakening tests or changing budget baselines.
3. Inspect the generated Pro artifact and factory patch labels, then test FM6 patch selection and both DX7 banks in the emulator.
4. Only after those checks, prepare a controlled hardware test on the SMK-37 Pro. Do not flash while the complete suite is red.
5. Update this document with exact run IDs, commit SHA, artifact name, and any remaining hardware-only checks. Keep Elite/MKE/FМ-1 outside the build matrix until Pro is signed off.


## 9. Follow-up audit — 7 November 2026

Follow-up source review completed against the active `extended-firmware` branch. Current audit checkpoint: `7ef1eb132c50344001b1573bd6b1b980f7ba3d4b`.

- Confirmed DX7 ROM integration is already complete at source/build-generation level: `assets/dx7/rom1a.syx.gz` and `rom1b.syx.gz` are checked in; the generator validates Yamaha bulk-dump framing, 7-bit payload and checksum, then generates 64 ROM voices alongside eight native FM6 voices (72 factory voices total). The 27 writable user slots remain.
- Targeted local validation of both committed ROM assets passed: each decompresses to 4,104 bytes; both contain 32 valid 128-byte voices with valid framing, 7-bit payload and Yamaha checksum.
- Found a first-catalogue lifecycle defect: section store/erase returned after initial catalogue creation without replaying the requested overlay. In particular, erasing a section before catalogue initialisation could leave it persisted in flash and make it reappear after restart.
- Fixed the initialise-then-retry paths in `firmware/src/project.c` (commit `c0547407729952385828d209aaeeb29729a068cb`).
- Added section-H erase-overlay round-trip coverage, including default-section selection and byte-identical preservation of A–G (`tests/section_catalog_test.c`, commit `7ef1eb132c50344001b1573bd6b1b980f7ba3d4b`). This C regression test has been added but has not yet been executed in this environment.
- The last inspected GitHub Actions run is still run `42348468647` on commit `bade3bea`: the SMK-37 Pro package build and package validation passed, but the host/sanitizer/web suite stopped at the missing `memset`/`memcpy` declarations in `section_catalog.c`. Those declarations were added in `db178128`; subsequent catalogue scratch-layout and runtime/test changes are newer than that run. Do not treat the current branch as fully build- or test-verified until a new complete run passes.
- The current branch's product-spec/code discrepancies still need a decision: the plan describes a 16-entry arrangement and 12 locks with no per-step microtiming, while current SMK-37 source defines 32 arrangement positions and FUN7 still stores 24 locks plus packed microtiming. Keep these visible as unresolved product/data-model alignment items.
- No emulator or physical SMK-37 Pro validation is claimed. Do not flash until the current Pro build, complete test suite and memory budgets pass.
