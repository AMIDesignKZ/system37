# SLOOP Pro Extended — implementation plan

Target hardware: M-VAVE SMK-37.

## Requested result

- Five independently sequenced synth parts plus the existing dedicated drum part (six tracks total).
- Eight independently editable song sections, A–H.
- Preserve the existing 16-entry arrangement chain; each entry can reference any of the eight sections.
- Retain the four physical faders and provide track selection/mixing for all six tracks.
- Keep the stock firmware and the existing V16 recovery route untouched.

## Baseline audit

- The current core defines `NPART=3`, `NTRK=4`, and `TRK_DRUM=3`. The synth-part count is used throughout the audio engine, sequencing, effects, modulation, and UI; this is not a local constant change.
- The arranger has 16 chain entries but only four scenes (`ARR_STEPS=16`, `ARR_SCENES=4`).
- Project sections are currently four slots (A–D), with hard-coded four-slot masks and bounds in project persistence, restore, section storage, and the song UI.
- The current project payload is effectively at its 3,840-byte limit. Adding two tracks directly to the existing project structure is not safe without a storage-format redesign.
- Flash object mapping allocates four project objects. The SMK-37 flash layout also contains user presets, autosave, FM bank, settings, owner metadata, and reserved/stock regions. These must be mapped explicitly before allocating additional project storage.
- The browser editor contains independent assumptions for four tracks and four sections. Firmware and editor formats must remain aligned.


## Accepted product and instrument decisions (6 November 2026)

- System 37 is an alternative firmware based on SLOOP, not a backwards-compatible SLOOP release.
- The five synth parts are freely assignable between exactly two internal engines: Analog and FM6.
- System 37 uses compact engine IDs in its own project format: Analog = 0, FM6 = 1. Historical SLOOP engine IDs are not retained.
- No SLOOP FUN1–FUN6 project import, legacy engine mapper or old-project migration is required. New System 37 projects start from defaults.
- Retain the dedicated drum synthesiser and its existing kit library, explicitly including 808 and 909, plus the four editable SYN1–SYN4 kits.
- Retain sampled drum playback independently of the synth-engine list.
- Retain two 40 KiB user sample slots, USR1 and USR2. Retire USR3/USR4 from the new interface, but do not overwrite existing user data until its retention/export path and flash allocation are validated.
- FM6 remains a six-operator DX7-style engine with 32 algorithms. The target is four complete 32-patch user banks (128 patches), standard DX7 SysEx bank import/export and factory patches.
- The external FM-1 remains a separate future MIDI instrument, not an internal SMK-37 engine.
- Sequencer direction remains 12 parameter locks per track, no per-step micro-timing, fill conditions retained, 64-step patterns and a 16-entry arrangement chain. Eight sections A–H remain the target.
- Keep the stock firmware and V16 recovery route untouched.

These are accepted product decisions. They do not claim that any runtime or UI implementation has changed.

## Implementation phases

1. **Complete dependency and storage audit.** Trace every use of `NPART`, `NTRK`, drum indices, section masks, project slot counts, project format versions, backup/restore paths, MIDI channel settings, mixer/fader routing, and editor serialization.
2. **Design the data model and flash layout.** Decide how six-track projects and eight sections are represented without exceeding the existing payload limit or overwriting any reserved SMK-37 flash region. Define migration and compatibility behaviour before changing structures.
3. **Expand the audio and sequencing engine.** Add two synth parts across voice allocation, engine state, modulation, effects, mixing, MIDI routing, and real-time scheduling. Check CPU and RAM budgets on the SMK-37.
4. **Expand project and arrangement handling.** Implement A–H storage, validation, save/load, autosave, backup/restore, and all 16 arrangement references. Add explicit format/version handling.
5. **Update the instrument UI and browser editor.** Add selection and level control for six tracks while retaining four physical faders; expose all eight sections and keep editor import/export compatible.
6. **Build and validate.** Compile for the exact SMK-37 target, run static checks, inspect generated firmware size, and test project migration and recovery. Hardware testing must start with a recoverable test build and a verified stock/V16 recovery path.

## Safety constraints

- Do not alter stock firmware images, bootloader, calibration, protected flash areas, or the user's V16 recovery files.
- Do not assume spare flash sectors are safe until the complete linker, partition, and storage map has been checked against the target board.
- Do not increase track or section constants globally until all dependent arrays, masks, UI bounds, MIDI settings, and persistence code have been updated.
- Preserve a known-good baseline and make changes on the `extended-firmware` branch only.
- A build is not considered hardware-safe merely because it compiles; verify memory use, flash writes, project round-trips, and recovery first.

## Current status

This initial checkpoint recorded the design before implementation. See the progress checkpoints below for isolated host-tested prototypes. Runtime track counts, live project persistence, flash mapping and audio behaviour have not yet been changed.


## Verified source audit checkpoint (6 November 2026)

The following details have now been checked against the current source, rather than inferred from the earlier plan:

### Drum architecture and kits

- The dedicated drum track already has a substantial synthesis engine in `drum_synth.c`. It synthesises tones, pitch drops, transient clicks, white/metal/cymbal/chip noise, clap bursts, resonant filtering, drive and sample-rate/bit-depth crushing. It is not dependent on the general SAMPLE synth engine.
- The generated drum-kit source currently defines **32 synthesised kits**, including named `808`, `909`, `606`, `80S`, `VINTAGE`, `TECHNO`, `ELECTRO`, `SYNTHWV`, `CHIP`, `INDUSTR` and others. This already provides the requested 808 and 909 starting points, plus broad electronic coverage.
- The drum UI also exposes four additional editable `SYN1`–`SYN4` kits through a separate `dsu_kit` path. The host test confirms their initial contents are copies of 808, 909, TRAP and TECHNO respectively; they can be edited in the web editor and are retained as user kits.
- Sampled drum kits and user-sample drum kits are handled separately in `drums.c`. Any removal of the selectable SAMPLE synth engine must leave this drum sample playback path intact.

### FM6 bank capacity

- FM6 is already a six-operator engine with 32 algorithms and standard DX7 128-byte voice packing/unpacking.
- The standard 32-voice SysEx bank parser is not the limiting factor. The current persistent bank is limited to **27 user patches** because `FM6_BANK_N=27`, and its A/B storage object is 3,472 bytes.
- One complete 32-patch bank requires 4,096 bytes of patch records alone, before metadata. It cannot fit in the current 3,840-byte storage-object payload.
- The current editor labels patch slots B1–B27. The bank format, editor protocol, storage object allocation and backup/restore object numbering must all change together for multi-bank storage.
- The user has approved four complete 32-patch banks (128 user patches total). This is the target capacity, not an implemented or hardware-verified capability.

### Track, project and memory constraints

- The current source confirms `NPART=3`, `NTRK=4`, `TRK_DRUM=3`, `NLOCK=24`, and 64 steps. The project still stores a 6-bit micro-timing value plus a 2-bit fill condition for every step.
- This differs from the accepted extended design of 12 locks and no micro-timing. Those reductions have not yet been implemented.
- The current project structure is near the 3,840-byte object limit, and project sections are held in a four-element `.noinit` array. The linker reserves only 0x3D50 bytes for `.noinit`; simply doubling the section array is not viable. Eight sections need a deliberate RAM placement redesign as well as flash storage changes.
- The arranger is already 16 entries, but `ARR_SCENES=4`; scene validation and UI are still hard-coded to A–D.
- The four synth engines/parts and drum track are referenced across audio, FX, MIDI, project migration, UI and the browser editor. No global track-count change is safe until all of these references are audited.

### Sample-slot map discrepancy to resolve before flash work

- `eng_sample.c` currently defines four 40 KiB user slots at 0xC2000, 0xCC000, 0xD6000 and 0xE0000 (160 KiB total).
- The SMK-37 flash-map comments describe the same 0xC2000–0xE9FFF region as two 80 KiB slots. The implementation and documentation therefore describe different logical layouts over the same physical range.
- The user has since confirmed two 40 KiB slots. The intended retained positions are 0xC2000 and 0xCC000; 0xD6000 and 0xE0000 are candidates to retire. The 80 KiB released range is not yet allocated. No flash remapping has been made.

### Immediate implementation order

1. Finish the source-level dependency audit, including `SYN1`–`SYN4`, all engine IDs, RAM section placement, editor protocol and flash guard call sites.
2. Add host-side size/layout checks and tests for the proposed project and FM6 bank formats before changing persistent structures.
3. Design the six-track project representation and eight-section RAM/flash storage together. Keep the existing format readers for migration.
4. Implement FM6 multi-bank storage as a versioned format with a safe A/B commit strategy; do not simply enlarge the current object.
5. Only then begin changing the runtime track count and browser/device UI.

No firmware source, flash address, stock region, recovery path or installed device has been changed by this audit.


### User decision: sample capacity

On 6 November 2026 the user confirmed **two user sample slots of 40 KiB each**.

- Keep the first two existing slot positions as the intended user-facing slots: USR1 at 0xC2000 and USR2 at 0xCC000.
- Retire USR3 at 0xD6000 and USR4 at 0xE0000 from the user-sample interface.
- This potentially releases 80 KiB (0xD6000–0xE9FFF) for a future storage redesign. It is not yet reassigned to projects or FM6: the flash map, loader allow-list, stock marker handling, backup/restore and all editor references must be reconciled first.
- Keep the existing 40 KiB per-slot capacity and sample encoding. Do not silently convert either slot to 80 KiB.
- The current flash-map comments incorrectly describe two 80 KiB slots, while the implementation has four 40 KiB slots. Update those comments only as part of the coordinated flash-map change, not as an isolated documentation-only claim.

### Corrected storage capacity model

The original calculation below this heading has been superseded by the corrected design in EXTENDED-FIRMWARE-ARCHITECTURE.md. Two details materially change the estimate: fill conditions remain after micro-timing is removed, and autosave is also a full project that must grow.

- Current format-6 project: 3,816 bytes, four tracks, 934 bytes per track.
- Format-7 track: 934 − 64 bytes (old micro/fill field) + 16 bytes (fill-only field) − 48 bytes (12 fewer locks) = 838 bytes.
- Format-7 project: 76-byte header + six 838-byte tracks + 4-byte checksum = **5,108 bytes**. Confirm with compiler sizeof assertions.
- Eight projects in one catalogue: 8 × 5,108 bytes plus a small directory, about 40,944 bytes. Eleven sectors per A/B copy provide 44,800 payload bytes after the first-sector header, so the catalogue uses 22 sectors total.
- Four FM6 banks: 4 × 32 × 128 = 16,384 patch bytes, approximately 16.4 KiB including metadata. Five sectors per copy, ten total.
- Autosave: 5,108 bytes; two sectors per copy, four total.

The catalogue, FM6 and autosave therefore require 36 sectors versus 12 sectors in their current project/FM6/autosave allocations: **24 additional sectors**.

The source map identifies 34 candidate sectors: 20 sectors from retiring USR3/USR4 (0xD6000–0xE9FFF), four existing spare sectors (0xB7000–0xBAFFF), nine unlisted sectors (0xEB000–0xF3FFF) and one unlisted sector (0xFE000). The proposed allocation uses the first 24 (the 20 retired sample sectors and four spare sectors) and leaves the 10 unlisted sectors unused as reserve.

The architecture document defines the proposed A/B extents, migration order, flash allow-list changes and two-buffer section cache. The map is still a proposal: compiler sizes, target linker headroom, guard behaviour, ownership clearing, stock-marker release and power-loss recovery must be tested before any source or flash-map change. This is capacity-feasible on paper, not yet target-build or hardware verified.

### Two-slot sample dependency audit (6 November 2026)

The two-slot decision affects more than the sample-slot constant:

- eng_sample.c uses SMP_USER_SLOTS=4 for zone arrays, scan state, names and slot addressing. Its SMK-37 addresses are 0xC2000, 0xCC000, 0xD6000 and 0xE0000. The encoded slot decoder also assumes four slots.
- drums.c exposes four single-slot user drum kits plus a special USR3+4 paired kit that reads slot indexes 2 and 3. With two slots, retain one user drum kit per slot (USR1 and USR2) and retire the USR3/USR4 entries and paired-kit path. Sampled drum playback and user drum kits in the two retained slots remain supported.
- The browser editor hard-codes four sample records, backup object IDs 32–35, four sample selectors in SAMPLE/GRAIN engine descriptions, and the USR3+4 kit split/upload workflow. Its backup UI and user-facing text also describe four slots. These require a coordinated protocol and editor update.
- New backups should contain only USR1 and USR2. Restore must continue to read legacy four-slot backups, restore the first two slots, and ignore retired slots rather than writing into reassigned storage.
- Keep legacy stock-mapping detection aware of old sample headers while old data may exist. Preserve the stock marker release before the first erase/write in the USRFLASH region. The 0xEA000 marker sector remains protected.

No sample-slot constant, drum-kit index, backup object, browser-editor path or flash guard has been changed yet.


## Design freeze — four FM6 banks and eight-section catalogue (6 November 2026)

The user has approved four complete 32-patch FM6 banks (128 patches total). The functional target is recorded in EXTENDED-FIRMWARE-SPEC.md and the pre-implementation architecture in EXTENDED-FIRMWARE-ARCHITECTURE.md.

Important correction to the earlier capacity model: fill conditions are retained. The new track record is modelled at 838 bytes (not 822), and the six-track project at 5,108 bytes (not 5,012). Autosave also grows and must be included. Eight independent section A/B objects would consume all candidate sectors, so the preferred design is one A/B catalogue containing all eight sections.

The corrected model is 22 sectors for the section catalogue, 10 for four FM6 banks, and 4 for autosave: 36 sectors total versus 12 current, an increment of 24. The proposed allocation uses the 20 sectors retired from USR3/USR4 and the four existing spare sectors at 0xB7000–0xBAFFF, leaving 10 unlisted sectors unused. The architecture document specifies proposed extents, segmented multi-sector A/B semantics, migration order and flash guard changes.

This is a capacity-feasible paper design, not an implementation or target-build result. The compiler sizes, linker headroom, flash extent map, guards, migration and power-loss behaviour must be validated before firmware source changes or device installation.


## Progress checkpoint — multi-sector storage prototype (6 November 2026)

Added an isolated host-testable storage primitive:

- firmware/src/storage_multi.h
- firmware/src/storage_multi.c
- tests/storage_multi_test.c
- tests/run_tests.sh now compiles and runs the new test.

The prototype supports non-contiguous A/B sector extents, first-sector header reservation, logical reads across sector boundaries, page-bounded streaming writes, CRC/readback verification, header-last commit, sequence wrap comparison, and safe refusal to save when flash reads leave the current A/B state uncertain. The stream callback permits copying a catalogue and overlaying one changed section without a full catalogue RAM buffer.

The standalone host test passed with -std=c11 -Wall -Wextra -Werror -O2 and also under AddressSanitizer/UndefinedBehaviorSanitizer. It injects failures at each erase, payload-page and commit-header operation, including partial program failures, and checks that the previous committed copy remains readable.

This module is **not yet included by felucca.c**, does not replace storage.c, and has not been tested with the JieLi target compiler or physical SMK-37 flash. The board callbacks and fixed extent tables still need to be implemented and validated against fm1_flash_map.h before integration.

Next: extend the host tests to the exact 11-sector catalogue and 5-sector FM6 extent shapes, then implement format-7 project structures and migration tests. Only after these are stable should the generic storage module be integrated into the firmware's single compilation unit.


## Follow-up checkpoint — proposed extent map covered by host tests (6 November 2026)

The storage primitive test now instantiates the proposed SMK-37 extent tables for the section catalogue, four-bank FM6 object and autosave. It checks all 36 sectors are unique, exactly 12 sectors are reused from legacy objects, 24 are newly allocated, and every address lies in one of the identified candidate regions. This confirms the arithmetic and overlap-free nature of the proposed table; it does not establish that those regions are safe to erase on a real device.

The standalone test passed again with AddressSanitizer and UndefinedBehaviorSanitizer enabled. It is wired into tests/run_tests.sh. The complete repository test suite was not run because it requires the firmware build output and JieLi SDK/toolchain, which are not available in this working session.

Next engineering task: define the format-7 structures alongside the read-only format-6 structure, then add host migration tests before changing project persistence or runtime track counts.


## Progress checkpoint — System 37 Project Format 7 prototype (6 November 2026)

The working project name is now **System 37**. The firmware remains an SMK-37-targeted SLOOP-derived project; this name is a project identity, not a claim that the runtime firmware has been renamed or integrated.

Added an isolated format-7 schema and FUN6-to-FUN7 migration prototype:

- `firmware/src/project_v7.h`: fixed wire definitions for read-only FUN6 and new FUN7 records.
- `firmware/src/project_v7.c`: checksum validation, fill packing, track relocation, lock filtering/truncation and per-track loss reporting.
- `tests/project_v7_test.c`: host test for layout and migration.
- `tests/run_tests.sh`: compiles and runs the new test.

The new structures have compile-time size assertions: FUN6 remains 3,816 bytes, a FUN6 track remains 934 bytes, a FUN7 track is 838 bytes and a six-track FUN7 project is 5,108 bytes. The FUN6 mirror fixes its legacy track and lock counts explicitly so future runtime changes to `NTRK` or `NLOCK` cannot silently change the old on-flash layout.

Migration rules implemented in the isolated prototype:

- Preserve global values, all common parameter arrays, engine/preset bytes and all 64 steps.
- Pack the 2-bit fill condition for each step into 16 bytes and discard the old micro-timing bits.
- Map legacy synth tracks 0–2 to System 37 synth tracks 0–2.
- Initialise new synth tracks 3–4 as empty Analog tracks, loading their common and Analog-specific parameter defaults from the supplied parameter descriptors, with the default-preset sentinel.
- Move the legacy drum track from index 3 to System 37 track 5 (the sixth track), and remap a saved selection of the old drum track from index 3 to index 5.
- Retain the 12 earliest-step valid locks across the old 24 slots, preserve original slot order for same-step ties, and reject invalid/non-lockable parameters.
- Return a per-track bitmask identifying where valid locks were truncated, so the eventual UI can show a migration warning.

The isolated test source is wired into the repository host suite. A local strict-warning host harness using the same wire types passed, including AddressSanitizer and UndefinedBehaviorSanitizer. The complete repository suite and target build have **not** been run in this session; the repository suite requires the existing build output and JieLi SDK/toolchain.

This prototype is deliberately **not integrated** into `project.c`, `proj_import`, runtime track arrays, autosave, section storage or the multi-sector layer. Existing FUN1–FUN6 import behaviour and live project handling remain unchanged. Next step: compile and run the expanded project host tests in the repository build environment, then integrate the FUN6-to-FUN7 hand-off without changing runtime constants prematurely. The test source now exercises FUN1–FUN6 through the existing importer.


## Progress checkpoint — System 37 A–H section catalogue prototype (6 November 2026)

Added an isolated catalogue format and streaming section-update source:

- `firmware/src/section_catalog.h`
- `firmware/src/section_catalog.c`
- `tests/section_catalog_test.c`
- `tests/run_tests.sh` now builds and runs the catalogue test after the storage and format-7 tests.

Catalogue format v1 uses an 80-byte directory followed by eight fixed 5,108-byte FUN7 project records. Total payload is exactly 40,944 bytes. Each 8-byte directory entry stores a project checksum, cached bar count, presence flag and reserved byte. The directory stores the present-section bitmask and default section. The multi-sector A/B layer supplies the outer sequence number, payload CRC, copy identity and commit header; each present project also retains its own FUN7 checksum.

The catalogue API provides:
- Header/directory validation, including reserved fields and present-mask consistency.
- Full-catalogue validation by streaming each present project through one caller-supplied 5,108-byte scratch buffer.
- Loading one section with both project and directory checksum verification.
- First-catalogue creation from an erased/blank logical payload without allocating the full catalogue.
- Copy-on-write update of one section. The source callback reads the existing committed catalogue, overlays the changed directory and project record, and feeds the existing 256-byte streaming A/B writer. No 40 KiB RAM staging buffer is needed.
- A cached bar-count field per section and an explicit default-section value.

The proposed eleven-sector A/B extents provide 44,800 bytes per copy, leaving 3,856 bytes unused per copy after the 40,944-byte v1 payload.

The new host test exercises first creation, adding all eight sections, reading all eight back, replacing section D while preserving the other seven byte-for-byte, metadata/default-section updates, invalid section/default rejection and invalid project checksum rejection. The test is wired into `tests/run_tests.sh`, but it has **not yet been compiled or executed** in this session. The complete repository runner and JieLi target build remain unrun.

This is still an isolated persistence prototype. It does not alter the live project runtime, arranger, autosave, flash map, flash allow-list or boot/recovery behaviour. Before integration, the next gate is to run the new catalogue test and existing storage/format tests in the repository's host build environment, then review the actual linker/RAM map and board flash guard before assigning any physical sectors.


## Progress checkpoint — arranger and section-runtime audit (6 November 2026)

The existing arranger has been inspected before changing its scene count or runtime storage:

- `arr_config_t` already supports 16 arrangement entries. Each entry stores an 8-bit scene index and a 1–64 bar duration. The current limitation is `ARR_SCENES=4`, not the chain length.
- Scene readiness, default chain generation, validation, scene deletion, bar calculation, the song UI and section selection all assume A–D / four slots. These call sites need to be changed as one coordinated step; changing only `ARR_SCENES` is unsafe.
- The audio path calls `arr_next()` and then applies the next section at an audio-block boundary. The current section projects are already resident in RAM. The audio callback must remain free of flash reads, catalogue validation, allocation and long copies.
- The current live project array is four format-6 projects in `.noinit`. The song-mode snapshot and autosave are separate full-project buffers in `.pool`. The existing `.noinit` allocation is already close to its documented 0x3D50-byte allowance, so eight in-RAM project snapshots are not a viable design.
- The catalogue must therefore remain flash-resident, with a bounded RAM staging strategy. The likely direction is to preload the next section outside the audio callback, then apply the prepared project at the boundary. The exact buffer count, placement and handshake are not frozen until the linker map, pool/stack headroom and main-loop scheduling have been checked.
- Existing live-section jumps and quick-chain playback in `seq.c` are a second path in addition to the 16-entry song arranger. Both must use the same eight-section lookup/preload mechanism; otherwise A–H would work in one mode but not the other.
- Song-mode backup/restore currently captures the working project at start and restores it at stop. Its buffer and timing must be included in the six-track RAM budget.

No runtime constants, project arrays, audio callbacks, arranger UI, flash access or linker sections have been changed by this audit. Next gate: obtain the exact target linker map and pool/stack usage, then specify and host-test a small section-preload state machine before wiring it into the audio path.


## Memory-budget finding — section prefetch design (6 November 2026)

The target linker script and RAM notes have now been inspected. The relevant constraints are more precise than the earlier `.noinit` estimate:

- `firmware/app.ld` gives `.noinit` exactly 0x3D50 (15,696) bytes. Four current FUN6 projects occupy 4 × 3,816 = 15,264 bytes, leaving 432 bytes before other `.noinit` objects. Eight FUN7 projects would require 40,864 bytes and are impossible there.
- The documented `.pool` region is 0x54000 (344,064) bytes, with 333,900 bytes used in the RAM audit: 10,164 bytes spare. Increasing both song backup and autosave from 3,816 to 5,108 bytes costs another 2,584 bytes. Two additional 5,108-byte section buffers would cost 10,216 bytes and exceed the reported spare pool by 52 bytes, even before other changes.
- The ordinary 96 KiB RAM region had 58,220 bytes used in the RAM audit, leaving 40,084 bytes at that checkpoint. The project import staging union currently lives in this region and will also need review when FUN7 becomes the runtime format.
- Song-mode backup and autosave are not live at the same time in the current control flow: autosave refuses to run while `song.playing`, and section flush avoids autosaving while the arrangement clock runs. A shared backup/autosave buffer is therefore a plausible optimisation, but it must be verified against every reset, editor, import and save path before implementation.

This points to a leaner architecture than two full section caches: keep the eight-section catalogue in flash, use one FUN7 staging project for prefetch and boundary application, and investigate sharing the song-backup/autosave buffer. The active section is already copied into the live track state by `proj_apply()`; it does not need to remain as a second resident project snapshot. This is a design candidate, not yet an approved memory layout. The RAM audit values are a prior measured baseline, not measurements of the System 37 build. The next step is to confirm the actual current target build's map and stack/pool headroom before allocating anything.


## Progress checkpoint — section prefetch state machine (6 November 2026)

Added an isolated, hardware-independent hand-off module:

- `firmware/src/section_prefetch.h/.c` defines IDLE, REQUESTED, LOADING, READY and FAILED states.
- Main/UI code requests a section and receives a non-zero generation ticket. The main loop claims the request, reads into the single staging buffer, validates the complete FUN7 record, and only then publishes READY.
- The audio-boundary side can consume a READY record only when its section matches the section due to play. Consumption is one-shot. It performs no I/O and does not copy the project itself.
- A newer request supersedes an older generation; stale completions are rejected. Failed reads/validation are not published and can be retried. Cancel invalidates all outstanding tickets. Repeating the same in-flight request is idempotent.
- The state fields use volatile accesses and compiler publication barriers for the intended main-loop/audio-ISR hand-off. This is designed for the firmware's single-core interrupt model, not as a general multi-threaded C11 synchronisation primitive.
- `tests/section_prefetch_test.c` covers request, idempotence, no visibility while loading, matching/one-shot consume, failed-load retry, superseded completion, cancel, ready replacement, invalid section and generation wrap. It is wired into `tests/run_tests.sh`.

Local isolated host validation completed: strict C11 build with `-Wall -Wextra -Werror` passed, and the test passed. UBSan build and run also passed. ASan could not run in this environment because its runtime was not first in the process library list; this is an environment limitation, not a test failure. The full repository runner and SMK-37 target build were not run.

This is only the tested hand-off primitive. It is not yet called by `seq.c` or `arranger.c`, and there is no flash loader or runtime staging buffer allocated. Before wiring it in, the integration must define how the next arrangement/quick-chain/live-jump section is predicted early enough to load before the boundary, and how PLAY is gated until the first song section is ready. A section that misses its boundary must never be partially applied; the integration policy must explicitly decide whether to hold the boundary/clock or report a prefetch underrun.

## Progress checkpoint — legacy drum migration removed (6 November 2026)

The isolated FUN6-to-FUN7 converter now migrates only legacy synth tracks 0–2. The old drum track at index 3 is intentionally ignored: its pattern, kit selection, per-lane sound values and parameter locks are not copied into FUN7. New synth tracks 3–4 and the dedicated drum track 5 are initialised from defaults. The new drum track's E0 value is explicitly set to the default drum kit (kit 0), rather than inheriting the Analog engine's E0 default. A saved selection of legacy track 3 still selects the new dedicated drum track at index 5.

The migration test now expects lock-loss bits only for the three migrated synth tracks (mask 0x07), checks the three legacy synth tracks, and checks that all three new tracks are empty with default parameters and free locks. The old FUN6 source record is still validated as a complete 3,816-byte image before conversion; no legacy persistence or recovery path is changed.

The source and test changes are committed and wired into the repository host runner. **The updated test has not yet been compiled or executed in this session.** The full host suite and SMK-37 target build remain unrun. This completes the migration-policy change only; it does not integrate FUN7 into live project persistence or the sequencer.

## Runtime conversion gate — six-track live model

The FUN7 migration policy is now reflected in both the isolated converter test and the legacy project regression helper. Before changing runtime constants, the live-project conversion needs a separate implementation plan because the existing project.c project_t is still the persisted FUN6 layout and its track count/lock count are tied to the current NTRK=4 and NLOCK=24 definitions.

The runtime transition must freeze the FUN6 wire layout independently of future runtime constants, then make FUN7 the live project representation. That entails:
- Keep the FUN1–FUN6 readers byte-for-byte compatible, with explicit legacy constants (four tracks, three synth parts, 24 locks).
- Convert every imported/loaded legacy project into FUN7 before it becomes the live section or working project.
- Replace proj_capture() and proj_apply() as a matched pair for six live tracks, five synth parts, the dedicated drum track at index 5, packed fill conditions and 12 locks.
- Audit every NTRK, NPART, NLOCK and TRK_DRUM dependent array, loop, MIDI route, recorder, UI, voice allocator and test before changing core.h.
- Preserve the old storage readers and recovery route; do not write FUN7 to legacy project sectors as if it were FUN6.

The current branch has not yet made any runtime-constant or live-project changes. No full host suite or target build has been run for the migration change. The next implementation unit is a host-testable FUN7 live-project adapter and its regression tests, followed by a coordinated runtime model change only when all legacy wire-format dependencies are isolated.

## Progress checkpoint — catalogue-backed prefetch service and RAM gate (6 November 2026)

A small main-loop service now joins the existing catalogue reader to the tested prefetch state machine:

- New files: firmware/src/section_prefetch_io.h and firmware/src/section_prefetch_io.c.
- s37_pf_service() claims at most one request, scans the A/B multi-sector object, loads the requested A–H record through s37_cat_load_section(), and publishes READY only after the catalogue and FUN7 checks succeed.
- A failed scan, absent section, invalid directory or invalid project checksum publishes FAILED. If a newer request supersedes the synchronous load, its completion ticket is rejected and the stale buffer is not made READY.
- The service owns no staging memory. The caller supplies one project_v7_t buffer. It is intended for the main loop only; it must never be called from the audio callback.
- tests/section_catalog_test.c now exercises a missing-section failure and a successful load/one-shot consume. tests/run_tests.sh links the service and prefetch modules into the catalogue test.

This connects the persistence prototype to the prefetch primitive at the host-module boundary, but does not allocate a runtime buffer or connect seq.c/arranger.c. The updated catalogue test has not yet been compiled or run in this session.

### RAM decision remains a gate

The existing audit is still only a baseline: .noinit is 0x3D50 bytes, with 15,264 bytes already occupied by four FUN6 slots; the documented .pool baseline has 10,164 bytes free. A single 5,108-byte staging project is the only section-buffer design currently worth pursuing. The current song backup and autosave are each separate 3,816-byte objects; keeping them separate and growing both to 5,108 bytes, plus one staging object, adds 7,692 bytes net to the old .pool baseline, leaving only about 2,472 bytes before any other changes. Sharing the song backup/autosave storage could reduce that footprint, but their non-overlapping lifetimes have not yet been proven across every editor, import, reset and error path.

The larger unknown is the live audio model: five synth track_t objects instead of three add two complete synth tracks, including their voice state. The actual target compiler/linker map, stack high-water marks, and track_t/voice_t sizes must be measured before changing NPART/NTRK or allocating the staging record. The previous RAM audit is not a substitute for a System 37 target build.

No flash map, flash guard, linker placement, runtime track constants, live project application or audio callback has changed. The next safe runtime task is to isolate the legacy FUN6 wire reader from the new FUN7 live project type and build host tests around that boundary. Sequencer/arranger integration remains blocked until that runtime apply path and the memory budget are proven.

## Progress checkpoint — FUN6 wire format frozen in the legacy project layer (6 November 2026)

The legacy project.c structure definitions have been replaced with aliases to the explicit wire types in project_v7.h: project_t is now an alias of project_v6_t and proj_trk_t an alias of proj_trk_v6_t. The historical four-track, 24-lock FUN6 layout is therefore defined in one place and guarded by its existing compile-time size assertions (3,816 bytes total; 934 bytes per track). The legacy project code retains its existing field names, so this is intended as a no-behaviour-change refactor while removing its structural dependence on future NTRK/NLOCK changes.

This is not yet the full live-model split. The existing capture/apply routines still operate on the current four-track runtime and the legacy project representation. They must be replaced as a matched FUN7 capture/apply path before changing runtime constants. In particular, a future six-track runtime must never use the legacy FUN6 capture/apply functions as though they were its native project format.

The catalogue-backed prefetch service is now wired into the catalogue host test. That test checks both a missing-section failure (which must not publish READY) and a successful section load followed by a matching one-shot consume. The test runner compile line includes section_prefetch.c and section_prefetch_io.c. Neither this updated test nor the full host runner has yet been executed in this session; no target build or hardware test has been run.

## Progress checkpoint — six-track FUN7 live-project adapter (6 November 2026)

New files `firmware/src/project_v7_live.h` and `.c` define a host-testable boundary between the FUN7 wire record and a future six-track live sequencer model. The adapter does not depend on the current `NTRK`, `NPART` or `NLOCK` runtime array sizes. It uses explicit `PROJ_V7_TRACKS == 6` iteration and transfers each persistent `proj_trk_v7_t` record through caller-supplied callbacks.

Capture calls the global callback and each of the six track callbacks, then calculates and validates the FUN7 checksum. Apply validates the checksum, preflights globals and all six tracks before calling any mutating callback, then applies globals and all six tracks. The callback contract requires apply operations to be infallible after preflight; the board caller must exclude the audio ISR for the entire apply transaction. `all` preserves the existing distinction between full project loads and arrangement-section global handling; all six track records are applied in either mode.

`tests/project_v7_live_test.c` covers six-track capture, globals and track round-trip, full-project apply, section-mode apply, validation failure on track index 5 with zero mutations, unsupported engine rejection, bad checksum rejection, and a failed capture callback. It is wired into `tests/run_tests.sh`.

**Scope boundary:** this is the live-project adapter contract and host test, not yet the board-specific `track_t` callback implementation. The current runtime still has four `track_t` entries and 24 runtime locks. No `NTRK`, `NPART`, `NLOCK`, audio, UI, voice, linker, flash-map or live project apply changes have been made. The adapter test and full host runner have not yet been executed in this session, and no target build has been run. The next gate is to implement the board callbacks against the six-track runtime model, after the target memory audit and coordinated runtime-array migration. Do not connect `seq.c`/`arranger.c` until that board apply path exists.


## Progress checkpoint — six-track live runtime dependency audit (6 November 2026)

Added `EXTENDED-FIRMWARE-RUNTIME-AUDIT.md`, a source-level audit of the current four-track implementation. It separates the five synth-part count from the six sequenced-track count and identifies the concrete dependencies in voice allocation, engine arenas, FX/audio scheduling, MIDI/recording, project migration, UI/editor protocol and arrangement transitions.

The audit found an important remaining FUN6 compatibility issue: although the project structs are now explicit FUN6 wire types, several legacy project loops still use live `NTRK`, `NPART`, `NLOCK` and `TRK_DRUM`. Those loops must be changed to explicit legacy constants before any runtime count changes; in particular, the old drum index remains 3 in FUN6 even though the new runtime drum index will be 5.

A memory-design candidate is to move synth `voice_t` slots out of `track_t` into a dedicated five-part voice pool. The current drum `track_t` embeds an unused synth voice array while the actual drum engine owns `drums.v[]` separately. This candidate must be implemented and regression-tested without changing current three-part behaviour before expanding to five synth parts. Engine arena growth and the target linker map remain hard gates.

**No runtime source or constants changed.** The next safe implementation unit is to isolate FUN6 decode/conversion loops from the live constants and add regression assertions for the frozen four-track/24-lock wire format. Target sizes and stack/CPU measurements are still required before the six-track runtime patch.


## Progress checkpoint — FUN6 reader count isolation (6 November 2026)

The frozen FUN6 definitions now explicitly include four tracks, three synth parts, legacy drum index 3, 64 steps and 24 locks (`PROJ_V6_TRACKS`, `PROJ_V6_PARTS`, `PROJ_V6_DRUM_TRACK`, `PROJ_V6_STEPS`, `PROJ_V6_LOCKS`). Compile-time assertions pin those counts alongside the existing 934-byte track and 3,816-byte project sizes.

All `NTRK`, `NPART`, `NLOCK` and `TRK_DRUM` references have been removed from the legacy `project.c` layer and its regression helper; their loops and drum identification now use explicit FUN6 constants. This prevents a future change to live runtime constants from silently changing the legacy project's iteration bounds or interpreting the old drum track as the new index 5.

The changes are committed, but the project regression test and full host suite have **not** yet been compiled or run. This isolates the legacy count contract; it does not make the existing project capture/apply functions suitable for the future six-track runtime. Those functions remain legacy-only and must be replaced by the FUN7 live adapter before runtime expansion.


## Progress checkpoint — SMK-37 fader mapping audited (6 November 2026)

The actual SMK-37 control mapping is now confirmed from `main.c`, `ui_fbank.c` and `ui_layers.c`: the four physical faders use five parameter banks (LEVEL, FILTER, REVERB, DELAY and selected-track ENVELOPE). Banks A–D address tracks 1–4; bank E addresses the selected synth track's ADSR. The GLO layer also exposes mute/solo and level controls for only tracks 1–4. These are not already a two-layer six-track mixer. Moving drums from index 3 to index 5 would also invalidate the current fader-bank drum special cases, which only iterate fader indices 0–3.

The six-track mixer control mapping is therefore still an explicit design gate; no button/fader behaviour has been changed. The build script confirms that a baseline SMK-37 build reports image size, .data+.bss, lowram and .pool usage, and enforces at least 8 KiB of .pool headroom. The next measurement gate is an unchanged baseline build with the exact JieLi toolchain/SDK before any runtime memory-layout changes.


## Progress checkpoint — standalone engine scope and target baseline (6 November 2026)

The user has confirmed that System 37 needs only Analog and FM6 as internal synth engines, while retaining the dedicated 808/909-capable drum synthesiser and sampled drum playback. SLOOP project-file backwards compatibility and historical engine-ID mapping are explicitly out of scope.

The unchanged SMK-37 baseline build succeeded on the user's Mac with the JieLi toolchain and AC79 SDK. Reported values: 601,512-byte app image; 63,948 / 98,304 bytes `.data + .bss`; 22,772 / 29,360 bytes `.lowram`; 335,756 / 344,064 bytes `.pool` (8,308 bytes free). ELF symbols show 8,576 bytes for four current tracks, 25,776 bytes for three legacy engine arenas, and 15,264 bytes for the four legacy project slots in `.noinit`. Source-derived sizes are 2,144 bytes per `track_t`, 84 bytes per `voice_t`, and 1,720 bytes per FM6 part state.

### Next isolated implementation milestone

Refactor synth voice ownership out of `track_t` and reduce the selectable engine/state model to Analog and FM6 while preserving the current three-synth/four-track runtime counts. Keep drum voices, drum synthesis and sampled drum playback separate. Do not yet change track counts, lock count, microtiming, UI, editor protocol, flash map or project runtime integration. Run the host regression suite and rebuild the SMK-37 target after the source change; compare the linker report against this baseline before beginning six-track expansion.


## Progress checkpoint — first engine-scope reduction committed (6 November 2026)

The selectable synth engine table now contains only Analog (System 37 ID 0) and FM6 (ID 1). The engine source aggregation no longer includes DIGITAL, PHASE, LOFI, SAMPLE, FORMANT, TRIO, DRAWBAR, GRAIN, PHYS, NOISE or optional SLICE. The per-part engine arena now contains only FM6 state; Analog uses the state already held in voice_t. FM6's old factory-preset trim row is explicitly selected from the retained legacy trim table.

The voice lifecycle, sequencer keyboard mapping and FX core-splitting logic have been narrowed to the two-engine set. Drum synthesis and sampled drum playback remain in their separate drum subsystem; the 808/909 kit generator and drum-kit table were not changed. The frozen FUN6 reader remains in the source temporarily but is not a System 37 compatibility requirement.

**Not yet verified:** the modified source has not been run through the host test suite or the SMK-37 target build. The synth voice array is still embedded in track_t at this checkpoint; moving it into a synth-only pool is the next isolated RAM-layout change. No runtime track-count, lock-count, UI, editor protocol, flash-map or linker-script changes have been made.


### Host-test scope update

The host runner and target instruction-budget function list have also been narrowed to the System 37 engine set. Tests for the removed SAMPLE/GRAIN, NOISE and PHYS synth engines are no longer run; dedicated drum synthesis, 808/909 kits, sampled-drum playback, Analog and FM6 coverage remain. The regression golden renders, host CPU baseline and target instruction budget need regeneration and review after the next successful build. They have not yet been regenerated.


## Progress checkpoint — System 37 test isolation and five-part DSP coverage (7 November 2026)

The CI path is now intended to validate the SMK-37 System 37 target directly; the FM-1 reference build has been removed from the workflow. The test runner has a `SYSTEM37_ONLY=1` mode which consumes `build/system37.fwsc`, runs the SMK-37 package/flash/loader checks, and omits the FM-1 installer and legacy SLOOP site/installer assertions. The legacy four-slot user-drum-kit fixture is also excluded pending a System 37-specific fixture for the two-slot layout.

A separate `SYSTEM37_HOSTSIM` compile profile sets the host audio model to five synth parts plus the sixth drum track without enabling board-HAL code that a host simulator cannot provide. The DSP regression checks have been adjusted accordingly: the shared eight-voice budget and per-part cap cover all five synth parts; voice stealing and held-note checks exercise all other synth parts; MIDI routing checks cover channels 1–5, 10 and 16; CPU mixes include five synth parts; and the golden song render now contains five synth parts plus drums.

The first SMK-37 Pro target build in this isolated pipeline succeeded. The measured image was 581,740 bytes; `.data + .bss` was 98,188 / 98,304 bytes (116 bytes free), `.lowram` 28,384 / 29,240 bytes (856 bytes free), and `.pool` 335,340 / 344,064 bytes (8,724 bytes free). The image and pool fit, but the static RAM headroom is extremely tight and must be treated as a hard constraint.

**Verification status:** the target package/identity/flash-region checks passed in that run. The System 37 host suite has not yet passed: the first isolated attempt exposed a test-harness configuration error (the board macro pulled SMK HAL dependencies into the generic host simulator). That was corrected by introducing `SYSTEM37_HOSTSIM`; the follow-up run is in progress. Golden renders, host CPU baselines and target instruction budgets have **not** yet been regenerated or approved. Do not accept a baseline update until the audio-health, voice-allocation, determinism and target-budget checks are clean. No emulator or physical-device test is claimed at this checkpoint.
