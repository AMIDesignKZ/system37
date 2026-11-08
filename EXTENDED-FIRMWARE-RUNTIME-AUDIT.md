# System 37 — live runtime migration audit

Date: 6 November 2026  
Branch: `extended-firmware`

## Purpose and boundary

This is a source-level dependency audit for the six-track runtime conversion. The product direction has since been narrowed: System 37 is an alternative firmware with only Analog and FM6 as internal synth engines, and it does not need to import SLOOP project files. The adapter remains useful as a format boundary, but legacy project migration is no longer a product requirement.

**No runtime source, constants, linker placement, flash map, or target firmware behaviour is changed by this audit.** The current model remains `NPART=3`, `NTRK=4`, `TRK_DRUM=3`, `NLOCK=24`.

## 1. Counts are separate concepts

The target must keep these counts distinct:

| Constant | Current | Target | Meaning |
|---|---:|---:|---|
| Synth parts | `NPART=3` | `5` | Parts with synth voices, synth engines and per-part engine state |
| Sequenced tracks | `NTRK=4` | `6` | All tracks, including drums |
| Drum index | `TRK_DRUM=3` | `5` | Dedicated drum track |
| Runtime parameter locks | `NLOCK=24` | `12` | Locks available on each live track |
| Drum voice pool | separate | separate | Existing `drums.v[]`, not part of the synth voice budget |
| Synth voice slots | 3 × 8 active arrays | 5 × 8 | Eight slots per synth part, with the existing global eight-sounding-voice ceiling |

Do not use `NTRK` for synth-only arrays or `NPART` for all-track arrays. The drum render unit remains separate from the five synth render units.

## 2. Confirmed source dependencies

### Core and voice allocation

- `core.h` embeds `voice_t v[NVOICE]` inside every `track_t`. The drum track does not use this array; its actual voices are held separately in `drums.v[]`.
- `voice.c` iterates synth voices using `NPART` and accesses them directly through `t->v[]` / `trk[p].v[]`. The shared eight-voice budget counts sounding voices across synth parts, not the total number of allocated slots.
- `engines.c` allocates `eng_arena[NPART]`; arena ownership and PHYS state are indexed by synth-part pointer offset. This is per synth part, not per sequenced track.
- `project.c` checks active synth voices with `NPART`; drum voices are checked separately through `drums.v[]`.

**Memory design candidate:** remove the inline `voice_t v[NVOICE]` from `track_t` and hold synth voice slots in a dedicated `voice_t synth_voice[NPART][NVOICE]` pool, accessed through a small track-to-part helper. The drum track then owns no unused synth voice array. This keeps the distinction explicit and avoids allocating six voice arrays when only five are required. The direct voice accesses in `voice.c` and `engines.c` must be converted together; any other direct access must be found before implementation. This is a candidate, not yet a committed design.

### Audio rendering and effects

- `fx.c` uses `NPART` for the synth INSERT array, the drum track's four separate INSERT states, the drum render-unit bit (`1u << NPART`), `U_ALL`, core-1 work masks, synth mix loops and FORMANT scheduling.
- `mix_units()` renders synth parts 0..`NPART-1` and then the separate drum unit. `units_split()` also reasons about synth engines and per-part state.
- The track filter array `tflt[NTRK]` is all-track state; the INSERT arena is synth-part state plus four drum paths. These must not be conflated.
- The engine arena is currently a union of GRAIN, DRAWBAR, FM6 and PHYS state. Five instances may be materially larger than three. The actual compiled `sizeof(eng_arena_t)`, linker placement and peak runtime use must be measured before selecting a final arrangement.

### Sequencer, recording and MIDI

- `seq.c` uses `NTRK` for MIDI Program Change state, MIDI note bookkeeping, sustain/held-note state, per-track roll and recording state, track selection and output note-off loops.
- Synth MIDI channel selection currently reads `G_CH1 + i` for synth indices below `NPART`; the current global parameter set exposes only `G_CH1`–`G_CH3`. Five independently configurable synth channels therefore require a versioned settings/UI treatment for channels 4 and 5, while preserving the meaning of the existing three settings. Drum channel remains separate.
- Drum-specific input, roll and recording paths compare against `TRK_DRUM`. These should remain explicit drum-role checks after the index moves to 5.
- Recording and solo masks are byte-sized today. Six tracks fit, but every bit shift, mask clear, serialised field and editor protocol bound still needs review.
- The 12-lock target changes not only `track_t.lock[]`, but also active-lock restoration arrays `lk_param[]`, `lk_base[]`, `lk_set[]`, `lk_n`, lock allocation, lock undo, lock editor messages and lock processing in `seq.c`.

### Project persistence and migration

- `project.c` now aliases its project structs to explicit FUN6 wire types, so the wire structure itself is frozen at four tracks and 24 locks.
- However, several legacy reader/converter loops still use live `NTRK`, `NPART`, `NLOCK` and `TRK_DRUM` (including lock loops, track conversion, drum identification and project capture/apply). Those uses must be replaced with explicit FUN6 constants or kept inside functions whose loop bounds are explicitly legacy values before any runtime count changes.
- The legacy drum index is 3 even after the new runtime drum index becomes 5. Legacy migration must use a fixed `PROJ_V6_DRUM_TRACK=3`, never the new `TRK_DRUM`.
- The new `project_v7_live` adapter is count-independent and iterates six FUN7 records through callbacks. It is not yet connected to board runtime state.
- The old `proj_capture()` / `proj_apply()` pair still operates on the four-track FUN6 runtime. They must not be allowed to silently become six-track FUN7 operations through changed constants.

### UI, editor and panel

- `ui_input.c` contains an explicit four-entry track colour table sized by `NTRK`, as well as track selection bounds and all-track loops. The colour table needs six deliberate entries.
- `editor.c` sizes the editor's track-value cache from `NTRK`, sends `NTRK` in the track-list protocol, and validates track indices against it. The browser editor must be updated in lockstep with the device protocol.
- `main.c` contains a four-fader loop. This is a physical-control count, not a track count; it must remain four. The two-layer assignment for six track levels still needs a verified mapping against the actual SMK-37 controls. No new fader mapping is assumed by this audit.
- `ui.c`, `ui_input.c`, `editor.c`, `project.c` and `arranger_scene.c` contain generic loops over all tracks and drum-specific branches that must be classified as either all-track or synth-only.

### Arrangement and sections

- The 16-entry chain is already independent of the four scene limit. The section catalogue and prefetch prototypes support A–H, but current runtime section application still works with the legacy four-slot model.
- `arranger_scene.c` applies a section by releasing all tracks and calling the legacy `proj_apply()`; this must be replaced by the validated FUN7 live apply path after the six-track runtime is ready.
- `seq.c` has multiple transition routes (arrangement boundary, live jump and quick chain). All must share one prepared-section hand-off; none may perform flash I/O or catalogue validation in the audio path.

## 3. Lock and microtiming migration

The target lock count is 12, but FUN6 remains 24-lock. The runtime reduction must not change the legacy wire layout or its import loops. FUN7 already has a 12-lock record and no microtiming array.

Before changing `NLOCK`:
1. Introduce an explicit legacy constant such as `PROJ_V6_LOCKS=24` and use it in all FUN6 decode/encode/conversion paths.
2. Keep runtime active-lock arrays at the same count as the live lock store; no 24-entry shadow arrays should remain unless there is a documented compatibility reason.
3. Confirm the lock ordering policy in migration (12 earliest-step valid locks; original slot order breaks ties).
4. Verify undo, lock replacement, lock clearing, and restoration of base parameter values with exactly 12 slots.
5. Remove runtime microtiming state and every `micro[]` / nudge access only after the FUN6 importer has copied fill conditions and intentionally discarded nudge values.

## 4. RAM and engine-state gates

The previous linker audit reported approximately 10,164 bytes free in `.pool`, but that figure predates a target build of the current branch and is not a reliable allowance for this conversion. Eight FUN7 records cannot live in `.noinit`; the proposed one-buffer staging model remains the only current candidate.

The target runtime adds two track records and two synth parts. The likely savings are:
- remove unused synth voice slots from the drum track by separating synth voice storage from `track_t`;
- remove the 64-byte per-track microtiming array;
- halve runtime lock storage from 24 to 12 entries, including active-lock bookkeeping;
- disable/remove selectable engines outside Analog and FM6 after preserving their IDs and writing explicit legacy project/preset mapping.

The likely costs are:
- two additional full track runtime records;
- two additional synth voice-slot groups (16 `voice_t` records net versus the three active synth parts today);
- two additional per-part engine state arenas if the current union is retained;
- FUN7 working/staging, song restore and autosave buffers, subject to proving buffer lifetimes and a target linker map.

No exact byte saving or target fit is claimed. The next memory checkpoint must measure `sizeof(track_t)`, `sizeof(voice_t)`, `sizeof(eng_arena_t)`, `sizeof(project_v7_t)`, all linker sections, stack high-water and worst-case audio CPU on the exact SMK-37 toolchain.

## 5. Recommended implementation sequence

1. **Freeze legacy constants:** define and use explicit FUN6 counts and drum index throughout every legacy import/export path. Add regression assertions that FUN6 remains 3,816 bytes, four tracks and 24 locks.
2. **Measure the present target:** build the unchanged runtime branch and record section sizes, `track_t`, `voice_t`, engine arena size, stack margin and CPU baseline. If the exact target toolchain is unavailable in the environment, request its build/map output only at this gate.
3. **Refactor synth voice ownership:** separate the five synth voice pools from the six sequencer track records, while keeping the current three-synth runtime behaviour unchanged. Run existing voice, audio, engine and drum regression tests.
4. **Reduce runtime locks and remove microtiming:** change live storage to 12 locks and no micro array while preserving the FUN6 wire reader. Add focused lock/undo and step timing tests.
5. **Expand runtime tracks and synth parts together:** change the role constants and all audio, engine-arena, FX, MIDI, recorder, UI and editor arrays as one coherent patch. Do not expose a half-six-track state.
6. **Implement board callbacks:** connect `project_v7_live` to the new runtime, with preflight validation and an IRQ-safe apply transaction. Only then integrate section prefetch and the arranger.
7. **Re-measure and validate:** target build, linker map, stack and CPU, complete host suite, project migration/round-trip and hardware recovery checks.

## 6. Current status

This audit is source-derived and committed as a design checkpoint only. No board callbacks, runtime constants, `track_t`, voice pool, lock storage, engine arena, sequencer, UI, editor, flash map or linker layout have been changed. The project remains at the four-track runtime baseline. The next safe code change is step 1: isolate all FUN6 reader dependencies from live runtime constants and add explicit legacy regression checks.


## Progress update — FUN6 count isolation completed

The first recommended implementation step is now committed:

- `project_v7.h` defines fixed FUN6 counts for four tracks, three synth parts, drum index 3, 64 steps and 24 locks, plus a fixed 64-step FUN7 dimension.
- Compile-time assertions pin the historical counts and both wire sizes.
- `project.c` and `tests/project_test.c` no longer refer to live `NTRK`, `NPART`, `NLOCK` or `TRK_DRUM` values.
- The legacy project regression helper now uses the same frozen FUN6 constants.

The change has been inspected at source/diff level. The project regression test and full test runner have not been compiled or executed. The next step is to obtain a baseline SMK-37 target build/map (the repository's build requires the JieLi Linux toolchain and SDK), then perform the synth-voice ownership refactor as an isolated change. Do not change runtime counts until those measurements and regression tests are available.


## 7. SMK-37 physical fader and mixer control finding

The control-map source has now been checked directly:

- The SMK-37 has four physical faders. In the current SLOOP mapping, FADER BANK A controls LEVEL for tracks 1–4; B controls FILTER for tracks 1–4; C controls REVERB send for tracks 1–4; D controls DELAY send for tracks 1–4; E controls ATTACK/DECAY/SUSTAIN/RELEASE on the currently selected synth track.
- FADER BANK A–E are **parameter banks**, not track banks. They do not currently provide a two-layer mapping for six tracks.
- The existing `fb_param()` / `fb_move()` code iterates fader indices 0–3 and tests `i == TRK_DRUM`. With the drum track moving to index 5, that comparison can never match; the drum's level, reverb and delay special cases need an explicit track-bank/role mapping.
- The GLO layer also documents keys 1–4 as mute, keys 5–8 as solo, and knobs as levels for tracks 1–4. That control layer is likewise not a six-track mixer yet.
- The DAW-mode MIDI map has four fader controls in each of its two DAW banks. This is a separate MIDI-control mapping and should not be confused with the SLOOP FADER BANK A–E parameter pages.

The target's four-fader/two-layer track mixer therefore remains a control-design gate. Before implementation, define which two track groups are controlled by the four faders, how the fifth synth and drum levels are addressed, and how the existing A–E parameter banks remain accessible (or are deliberately simplified). No mapping has been changed or assumed here.


## Superseding decision — standalone engine scope and measured baseline (6 November 2026)

The user has confirmed:

- Internal synth engines are limited to Analog and FM6, freely assignable across five synth tracks.
- System 37 uses its own engine IDs (Analog 0, FM6 1) in its own project format.
- No SLOOP FUN1–FUN6 project import, historical engine-ID mapper or backwards-compatible project migration is required.
- The dedicated drum synthesiser must retain the existing kit library, including 808 and 909, and sampled drum playback must remain available independently of the synth-engine list.

This supersedes the earlier migration recommendations in this audit. FUN6 constants and readers may remain temporarily to keep the current branch compiling during the transition, but they are not part of the System 37 product contract and must not be used by the new project-loading path.

### Unchanged SMK-37 target baseline

The user built the unchanged extended-firmware source successfully on the exact SMK-37 target toolchain. The build report is:

| Section / output | Used | Capacity | Free |
|---|---:|---:|---:|
| App image | 601,512 B | — | — |
| .data + .bss | 63,948 B | 98,304 B | 34,356 B |
| .lowram | 22,772 B | 29,360 B | 6,588 B |
| .pool | 335,756 B | 344,064 B | 8,308 B |

The ELF symbol table reports:

- trk: 8,576 bytes, four × 2,144-byte track_t.
- eng_arena: 25,776 bytes, three × 8,592-byte legacy multi-engine arenas.
- proj_slot: 15,264 bytes, four legacy project slots in .noinit.

Source-derived structure sizes:

- voice_t: 84 bytes.
- fm6_note_t: 244 bytes.
- fm6_part_t: six FM6 note states (1,464 bytes) plus two 32-sample int32 buses (256 bytes), total 1,720 bytes.

A five-part FM6-only allocation would therefore require 8,600 bytes rather than 42,960 bytes for five copies of the current legacy multi-engine arena. This is a calculated target design estimate; it has not yet been built or measured in the linker.

### Next implementation unit

The next isolated source change is to move synth voice slots out of track_t into a dedicated synth-only voice array and reduce the selectable engine/state model to Analog and FM6. Initially preserve NPART=3, NTRK=4, TRK_DRUM=3, NLOCK=24, the existing audio scheduling and the current drum voice subsystem. Do not combine this change with six-track expansion, lock reduction, microtiming removal, UI/editor changes or flash-map changes.

The new engine table and voice ownership must be updated as one coordinated change. The Analog engine needs no separate arena; FM6 needs one 1,720-byte part state per synth part. Remove legacy engine-specific branches only after all compile-time references in voice.c, seq.c, fx.c, UI, editor and project code have been handled. The existing drum synthesis and sample playback paths must be verified explicitly.

Required evidence before the next milestone:
1. Full host test runner passes, including the previously added project/catalogue/prefetch/live-adapter tests.
2. SMK-37 target build passes.
3. .pool, .data + .bss, .lowram and app-image deltas are recorded against the baseline above.
4. No change to audio scheduling, drum voice allocation or flash writes is introduced by this refactor.


## Implementation checkpoint — Analog/FM6-only engine model (6 November 2026)

Committed source changes now implement the first part of the revised memory milestone:

- core.h defines two System 37 engine IDs: Analog 0 and FM6 1.
- engines.c includes only eng_analog.c and eng_fm6.c. Its per-part state array is now fm6_part_t[NPART], with engine-switch invalidation; the previous union containing GRAIN, DRAWBAR, FM6 and PHYS has been removed from the active build.
- voice.c no longer dispatches to the removed synth engines or applies sample-engine retrigger behaviour.
- seq.c no longer has SAMPLE/SLICE synth keyboard mapping.
- fx.c no longer applies PHYS/FORMANT-specific core-splitting constraints.
- tests/project_test.c now checks the two-engine System 37 table while retaining the frozen FUN6 wire-format regression assertions.

The drum code was deliberately left alone: drum_synth.c, drums.c, generated 32-kit source and sampled-drum paths are unchanged. The 808 and 909 kits therefore remain in scope.

This is a source change only. No host test or target build has yet verified it. The synth voice slots are still in track_t; their separation is the next memory optimisation. All runtime counts remain NPART=3, NTRK=4, TRK_DRUM=3 and NLOCK=24. Flash, linker, UI and editor protocol are unchanged.


### Host regression scope after engine reduction

The test harness now targets the two supported synth engines. Legacy synth-engine-only tests for SAMPLE/GRAIN, NOISE and PHYS were removed from the active runner; drum-kit, user-drum-kit, sampled drum, Analog, FM6, sequencer, storage and project-prototype tests remain. The regression generator now builds CPU mixes from Analog/FM6 parts and the target instruction-budget list contains only Analog, FM6, drums, slicer and the audio IRQ.

The existing golden render file, host CPU baseline and target instruction budget still describe the old engine set. Regenerate them with the intended-change flags after a successful target build, inspect the resulting diffs, and only then treat the updated host suite as a valid regression baseline. No such test run has yet occurred.

## Memory optimisation checkpoint — two-buffer section cache (8 November 2026)

The preferred first headroom optimisation is to replace the eight simultaneously resident FUN7 section caches with two reusable buffers: one active/working section and one staging/prefetch section. This is a cache-lifecycle change only; it must not reduce the six-track model, synth voice budget, sample capability, microtiming or parameter-lock count.

### Corrected measurements from the current source

The current branch's actual FUN7 format is **5,366 bytes**, not the 5,108-byte estimate in the earlier architecture proposal:

- `project_v7.h` currently defines `PROJ_V7_LOCKS=24`.
- Each track is 881 bytes and contains 48 bytes of packed six-bit microtiming plus 16 bytes of fill conditions.
- Six tracks plus the 76-byte header/checksum produce `sizeof(project_v7_t)==5366`.
- The 5,108-byte proposal assumed 12 locks and no microtiming; those assumptions do not describe the current source and must not be used for RAM sizing.

The current eight caches are allocated as:
- Five objects in ordinary `.bss`: 5 × 5,366 = 26,830 bytes.
- Two objects in `.lowram`: 2 × 5,366 = 10,732 bytes.
- One object in `.noinit`: 1 × 5,366 = 5,366 bytes.
- Total cache residency: 42,928 bytes.

Replacing eight caches with two therefore gives a **gross saving of 32,196 bytes** before any small metadata or staging changes. The existing `autosave_buf` is a separate 5,366-byte object in `.pool` and is also used as the song-return snapshot; it is not included in the eight-cache total and must remain until its independent lifetimes are redesigned and proven.

Latest reported target link figures (from the user's SMK-37 build) were:
- `.data + .bss`: 98,188 / 98,304 bytes; 116 bytes free.
- `.lowram`: 28,384 / 29,240 bytes; 856 bytes free.
- `.pool`: 335,340 / 344,064 bytes; 8,724 bytes free.

Two 5,366-byte buffers total 10,732 bytes, so the earlier proposal to put both in `.pool` does **not** fit the reported current `.pool` headroom. A likely placement to investigate is one buffer in ordinary RAM and one in `.lowram`, reusing the existing cache allocations:
- Remove five ordinary-RAM caches and add one working buffer: net 21,464 bytes recovered in ordinary RAM.
- Remove two LOWRAM caches and add one staging buffer: net 5,366 bytes recovered in LOWRAM.
- Remove the NOINIT cache: recover another 5,366 bytes in NOINIT.
- Aggregate net recovery remains 32,196 bytes. This is a placement proposal only; confirm the actual linker map, alignment and RAMTEXT/LOWRAM constraints before changing declarations.

### Runtime dependencies that must be refactored

A two-buffer design cannot be implemented by changing the array declaration alone. Current `project.c` and `arranger_scene.c` directly assume all eight section images are simultaneously addressable:

- Boot currently loads and validates all eight catalogue sections into `proj_slot[0..7]`.
- Save, erase, load, restore, `project_used()`, and `arrangement_ready()` directly index those pointers.
- `s37_catalog_write_full()` builds an eight-pointer array of all section images for the initial catalogue commit.
- `section_store()` captures directly into the selected slot; `section_load()` and `arrangement_apply()` apply directly from it.
- `section_bars()` reads pattern lengths from a resident section image.
- The arrangement apply path runs at an audio-block boundary. It must never perform catalogue reads, validation or flash access there.
- `proj_slot[7]` is also temporarily used as a FUN6 conversion destination during the pre-catalogue legacy import path. That legacy path must be isolated or redirected to the existing scratch/staging storage before the eighth cache is removed. The product decision says FUN6 import is not part of the System 37 contract, but the code still contains this transitional path.

### Required two-buffer behaviour

1. At boot, validate the catalogue and retain its directory/presence metadata; do not eagerly load all eight section payloads.
2. Load the selected/current section into the working buffer on stopped UI selection.
3. Load the next arrangement section into the staging buffer in the main loop, early enough to complete validation before the transition.
4. At the audio-block boundary, apply only an already validated RAM buffer. Swap working/staging roles or pointers atomically; do not copy 5.3 KiB or access flash in the ISR unless timing evidence proves that safe.
5. Resolve section presence and bar-length metadata from the validated catalogue directory where possible, rather than requiring all section payloads in RAM.
6. Save/erase through the existing catalogue overlay/streaming APIs using the working or staging buffer. For first catalogue creation, replace the current eight-pointer full-image API with a streaming source that can read the eight sections from their source records without retaining all eight in RAM.
7. Keep `autosave_buf` and its song-return lifetime unchanged in this first patch. Do not combine the cache refactor with voice-pool, lock-count, microtiming, audio scheduling or flash-map changes.

### Implementation and validation gates

- First map every `proj_slot[]` consumer and define explicit APIs for current section, staging section, section metadata, and load/save/erase.
- Add host tests for repeated A–H selection, dirty-section save, erase, catalogue first-commit, failed prefetch, rapid arrangement transitions, and staging-buffer reuse. Assert that the ISR-facing apply path only accepts a fully validated buffer.
- Test the song-return snapshot and autosave independently to prove no aliasing with either section buffer.
- Build the exact SMK-37 target and compare all linker regions against the latest measured baseline; inspect the map and stack high-water. Require comfortable headroom in each region, not just a lower aggregate byte count.
- Run the complete host suite and target build before signing off. Do not install on hardware until the normal recovery route has been verified.

Status: source-level audit completed; **no source declarations or runtime behaviour have been changed yet**. The two-buffer design is the selected direction, subject to the above lifecycle and linker checks.


### Additional catalogue and transition findings

The existing catalogue format already contains the metadata needed to avoid keeping every section resident:
- `present_mask` identifies populated sections.
- Each directory entry stores the section checksum and loop length in bars.
- `s37_cat_validate()` already validates all present records sequentially through one scratch project buffer; it does not require eight resident buffers.
- `s37_cat_load_section()` loads and validates one requested section into a caller-provided buffer.
- The single-section save/erase and working-project overlay APIs already stream from one replacement buffer plus one scratch buffer.

The remaining all-sections-in-RAM dependency is concentrated in the full-catalogue creation path: `s37_cat_image_t` stores eight section pointers, `s37_cat_prepare_image()` requires all eight project pointers, and `s37_catalog_write_full()` populates that pointer array from `proj_slot[]`. This API needs a streaming source that can provide each present section record on demand. The existing multi-sector storage writer already consumes a read callback, so this can be done without a 48 KiB catalogue image in RAM.

The transition path also needs a coordinated change:
- `arranger_scene.c:arrangement_apply()` currently applies `proj_slot[scene]` directly from the audio path.
- `seq.c:live_block()` applies live/quick-chain changes on a bar boundary from the audio sequencer path.
- The song arranger calls `arrangement_apply()` directly when `arr_next()` reports a section transition.
- Quick-chain selection currently masks scene indices with `& 3u` in `live_block()`, so it only preserves A–D even though the section catalogue supports A–H. This is an existing eight-section correctness issue that must be fixed as part of transition integration, not by changing cache storage alone.
- A live section request is currently consumed at the next bar; the request path must trigger staging early enough that flash read and validation finish before that boundary.
- The song arranger's next scene must likewise be known/prefetched before its transition boundary. The audio-block path should only switch to a validated staging buffer.

The catalogue directory's `bars` value should become the authoritative quick-chain duration metadata. This removes `section_bars()`'s current need to inspect every track in a resident project image. Presence checks should use the catalogue's validated `present_mask`, not `project_valid(proj_slot[i])`.

These findings strengthen the feasibility of the two-buffer design: the catalogue and streaming overlay already provide most of the storage primitives. The required work is the resident-section API, full-catalogue creation reader, and prefetch/atomic hand-off across live, quick-chain and song-arrangement transition paths.
