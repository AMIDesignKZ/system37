/* SPDX-License-Identifier: GPL-3.0-only
 * System 37: isolated project-format definitions and migration API.
 *
 * Format 6 is mirrored here as a read-only wire structure. Do not change its
 * field order: existing SLOOP projects are persisted as raw structures.
 */
#ifndef SYSTEM37_PROJECT_V7_H
#define SYSTEM37_PROJECT_V7_H
#include <stdint.h>
#include "core.h"

#define PROJ_V6_MAGIC 0x46554E36u /* FUN6 */
#define PROJ_V7_MAGIC 0x46554E37u /* FUN7 */
#define PROJ_V6_TRACKS 4u
#define PROJ_V6_PARTS 3u
#define PROJ_V6_DRUM_TRACK 3u
#define PROJ_V6_STEPS 64u
#define PROJ_V7_STEPS 64u
#define PROJ_V6_GLOBALS 32u
#define PROJ_V6_LOCKS 24u
#define PROJ_V7_LOCKS 24u
#define PROJ_V7_TRACKS 6u
#define PROJ_V7_PARTS 5u
#define PROJ_V7_DRUM_TRACK 5u
#define PROJ_V7_GLOBALS 32u
/* The auxiliary drum-timbre data follows the active board's live track index:
 * FUN6 stores it in track 4; FUN7 stores it in track 6. */
#define PROJ_LIVE_DRUM_TRACK TRK_DRUM
#define PROJ_V7_FILL_BYTES (PROJ_V7_STEPS / 4u)

#define PROJ_V6_NC ((uint32_t)P_ITYPE)
#define PROJ_V6_NX ((uint32_t)(P_E0 - P_ITYPE))

typedef struct {
    int16_t p[PROJ_V6_NC];
    int16_t e[8];
    uint8_t x[PROJ_V6_NX];
    uint8_t engine, preset;
    union { step_t step[PROJ_V6_STEPS]; dstep_t dstep[PROJ_V6_STEPS]; };
    uint8_t mf[PROJ_V6_STEPS]; /* low 6 bits: microtiming; high 2 bits: fill condition */
    plock_t lock[PROJ_V6_LOCKS];
} proj_trk_v6_t;

typedef struct {
    uint32_t magic, size;
    int16_t g[PROJ_V6_GLOBALS];
    uint8_t sel, drdly, rsv[2];
    proj_trk_v6_t t[PROJ_V6_TRACKS];
    uint32_t sum;
} project_v6_t;

/* These two structs are byte-stream formats, not native ABI records. Explicit
 * packing keeps FUN7's 881-byte track / 5,366-byte project layout identical
 * on the host compiler and the PI32V2 target (where int16_t alignment differs). */
typedef struct {
    int8_t p[PROJ_V6_NC]; /* all common parameters fit signed 8-bit; frees space for packed microtiming */
    int16_t e[8];
    uint8_t x[PROJ_V6_NX];
    uint8_t engine, preset;
    union { step_t step[PROJ_V7_STEPS]; dstep_t dstep[PROJ_V7_STEPS]; };
    uint8_t micro[48]; /* 64 signed 6-bit values, bit-packed; preserves FUN6 microtiming */
    uint8_t fill[PROJ_V7_FILL_BYTES]; /* four 2-bit FC_* values per byte */
    plock_t lock[PROJ_V7_LOCKS];
} __attribute__((packed)) proj_trk_v7_t;

typedef struct {
    uint32_t magic, size;
    int16_t g[32];
    uint8_t sel, drdly, rsv[2];
    proj_trk_v7_t t[PROJ_V7_TRACKS];
    uint32_t sum;
} __attribute__((packed)) project_v7_t;

_Static_assert(PROJ_V6_TRACKS == 4u && PROJ_V6_PARTS == 3u && PROJ_V6_DRUM_TRACK == 3u && PROJ_V6_STEPS == 64u && PROJ_V6_LOCKS == 24u, "format 6 counts are frozen");
_Static_assert(NSTEP == PROJ_V6_STEPS && NSTEP == PROJ_V7_STEPS && PROJ_V6_NC == 53u && PROJ_V6_NX == 10u && G_DRDLY == PROJ_V6_GLOBALS, "format 6 field identities are frozen");
_Static_assert(sizeof(plock_t) == 4u, "parameter lock wire size");
_Static_assert(sizeof(proj_trk_v6_t) == 934u, "format 6 track wire size");
_Static_assert(sizeof(project_v6_t) == 3816u, "format 6 project wire size");
#if BOARD_SMK37
_Static_assert(sizeof(proj_trk_v7_t) == 881u, "format 7 track wire size");
_Static_assert(sizeof(project_v7_t) == 5366u, "format 7 project wire size");
#endif

static inline void proj_v7_micro_set(proj_trk_v7_t *track, uint32_t step, int32_t value)
{
    uint32_t bit = step * 6u, byte = bit >> 3, shift = bit & 7u;
    uint32_t word = track->micro[byte];
    uint32_t raw = (uint32_t)value & 63u;
    if (shift > 2u)
        word |= (uint32_t)track->micro[byte + 1u] << 8;
    word = (word & ~(63u << shift)) | (raw << shift);
    track->micro[byte] = (uint8_t)word;
    if (shift > 2u)
        track->micro[byte + 1u] = (uint8_t)(word >> 8);
}

static inline int32_t proj_v7_micro_get(const proj_trk_v7_t *track, uint32_t step)
{
    uint32_t bit = step * 6u, byte = bit >> 3, shift = bit & 7u;
    uint32_t word = track->micro[byte];
    if (shift > 2u)
        word |= (uint32_t)track->micro[byte + 1u] << 8;
    int32_t value = (int32_t)((word >> shift) & 63u);
    return value >= 32 ? value - 64 : value;
}

uint32_t proj_v7_hash(const void *data, uint32_t length);
int proj_v7_valid(const project_v7_t *project);
/* Defaults arrays: common_defaults has P_E0 entries; analog_defaults has eight.
 * New synth tracks use the Analog defaults; legacy drum data is copied to track 6. */
int proj_v6_to_v7(project_v7_t *out, const void *data, uint32_t length, uint8_t *lock_loss_mask, const param_desc_t *common_defaults, const param_desc_t *analog_defaults);

#endif
