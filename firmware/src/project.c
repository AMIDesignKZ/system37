/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Projects: four slots in .noinit RAM (the song sections A..D), so they survive resets and UBOOT
 * entry. With FELUCCA_FLASH every save also goes to flash through storage.c, and an empty RAM slot
 * is filled from flash on load. The working project is also kept in flash by itself (autosave, when
 * the transport is stopped and nothing sounds) and comes back at power-on: SLOOP starts where you
 * left it.
 *
 * Formats: 6 ("FUN6", written, SLOOP 2.6): format 5 with the ten parameters of 2.6 (P_ITYPE .. P_LPOL, just
 * before P_E0: the INSERT, the ANALOG filter TYPE, ENV SYNC, LFO 2) in a byte each, and each step's nudge (6 bits)
 * and fill condition (2 bits) in one byte: format 5 was full (3840 bytes, one flash object), this is 3816.
 * Read and converted: 5 ("FUN5", SLOOP 2.4 .. 2.5: format 4 plus, per track, the nudge of each step (micro), PROJ_V6_LOCKS
 * parameter locks, the steps' fill conditions (2 bits each) and P_TFLT, one parameter more just before P_E0;
 * converted by id, the 2.6 parameters at their defaults, a lock on P_E0.. moved with it), 4 ("FUN4", SLOOP 2.0 .. 2.3: PROJ_NP_V4 parameters, today's G_COUNT, 10-byte
 * steps with levels and ratchets, the drum track's 16 lanes; no nudge, no lock), 3 ("FUN3", SLOOP 1.x: 8-byte
 * steps, the drum track's notes become its lanes, the swings x 0.8 for the MPC scale), 2 ("FUN2") and 1
 * ("FUN1"), which held PROJ_NP_V2 parameters per track, mapped by count as user presets are (the first
 * PROJ_NP_V2 - 8 are P_LEVEL.. in order, the last 8 P_E0..P_E7; the parameters added since take their
 * defaults). Their engine bytes are kept: formats 1 and 2 had engines 0..7 (ANALOG .. WHEEL), and the
 * engines added since were appended, no index moved; the drum track's byte (it has no engine) becomes 0.
 *
 * Built on the host too (tests/project_v6_legacy_test.c, -DPROJ_HOST): the part above the #ifndef
 * PROJ_HOST needs core.h, params.c (TP), drums.c (the lanes), the engines and trk_def_engine (ui.c). */
#ifndef UI_SMALL                 /* (2.7) compiled for size: not the audio path (gfx.c UI_SMALL); the host tests: nothing */
#define UI_SMALL
#endif
#include "project_v7.h"                 /* frozen FUN6 wire layout and isolated FUN7 format */
#if BOARD_SMK37
#include "project_v7_live.h"
#include "section_catalog.h"
#include "storage_multi.h"
#endif

#define PROJ_MAGIC PROJ_V6_MAGIC                 /* "FUN6": frozen legacy project image */
#define PROJ_MAGIC_V5 0x46554E35u              /* "FUN5": format 4 + per-step nudge, parameter locks, fill conditions (SLOOP 2.4) */
#define PROJ_MAGIC_V4 0x46554E34u              /* "FUN4": four tracks, P_COUNT parameters each, 10-byte steps; read only */
#define PROJ_MAGIC_V3 0x46554E33u              /* "FUN3": SLOOP 1.x; read only */
#define PROJ_MAGIC_V2 0x46554E32u              /* "FUN2": four tracks, PROJ_NP_V2 parameters; read only */
#define PROJ_MAGIC_V1 0x46554E31u              /* "FUN1": one instrument; loads into track 1 */
#define PROJ_NP_V5 61u                         /* P_COUNT of format 5 (P_E0 was 53) */
#define PROJ_NP_V4 58u                         /* P_COUNT of format 4 (P_E0 was 50) */
#define PROJ_NP_V3 57u                         /* P_COUNT of format 3 (P_E0 was 49) */
#define PROJ_NG_V3 27u                         /* G_COUNT of formats 1..3 */
#define PROJ_NP_V2 53u                         /* P_COUNT of formats 1 and 2 (P_E0 was 45) */
#define PROJ_NG_V2 27u                         /* G_COUNT of formats 1 and 2 */
#define PROJ_NG 32u                            /* globals of formats 4..6 */
_Static_assert(G_DRDLY == PROJ_NG && G_TXSRC == PROJ_NG + 5u,
               "the globals a project holds, then G_DRDLY (then 2.6's GLO > MIDI: settings of the FM-1)");
_Static_assert(G_FXFADE == G_CLKOUT + 1u && G_CH4 == G_FXFADE + 1u && G_CH5 == G_CH4 + 1u && G_COUNT == G_CH5 + 1u, "System 37 appends MIDI channels 4 and 5 after legacy globals");
#define PROJ_NC6 PROJ_V6_NC
#define PROJ_NX6 PROJ_V6_NX
_Static_assert(PROJ_NC6 == PROJ_NP_V5 - 8u && PROJ_NX6 == 10u, "format 6: format 5's parameters, then ten bytes");

/* The legacy persistence code keeps its historical names, but its wire
 * structure is now explicitly frozen at four tracks and 24 locks. It no
 * longer changes if the live runtime later moves to six tracks / 12 locks. */
typedef proj_trk_v6_t proj_trk_t;
typedef project_v6_t project_v6_legacy_t;
#if BOARD_SMK37
typedef project_v7_t project_t;
_Static_assert(sizeof(project_t) == 5366u, "FUN7 live project image");
#else
typedef project_v6_legacy_t project_t;
#endif
_Static_assert(sizeof(project_v6_legacy_t) == 3816u && sizeof(project_v6_legacy_t) <= 3840u,
               "FUN6 remains a 3,816-byte legacy flash object");
/* Historical FUN1-FUN5 wire layouts. These are read-only migration inputs:
 * keep their field order and dimensions independent of the live runtime. */
typedef struct { uint8_t note[4], n, time, flags, vel; } step8_t;
typedef struct {
    int16_t p[PROJ_NP_V5];
    uint8_t engine, preset;
    union { step_t step[PROJ_V6_STEPS]; dstep_t dstep[PROJ_V6_STEPS]; };
    int8_t micro[PROJ_V6_STEPS];
    plock_t lock[PROJ_V6_LOCKS];
    uint8_t fill[PROJ_V6_STEPS / 4u];
} proj_trk_v5_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[PROJ_V6_GLOBALS];
    uint8_t sel, drdly, rsv[2];
    proj_trk_v5_t t[PROJ_V6_TRACKS];
    uint32_t sum;
} project_v5_t;
typedef struct {
    int16_t p[PROJ_NP_V4];
    uint8_t engine, preset;
    union { step_t step[PROJ_V6_STEPS]; dstep_t dstep[PROJ_V6_STEPS]; };
} proj_trk_v4_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[PROJ_V6_GLOBALS];
    uint8_t sel, rsv[3];
    proj_trk_v4_t t[PROJ_V6_TRACKS];
    uint32_t sum;
} project_v4_t;
typedef struct {
    int16_t p[PROJ_NP_V3];
    uint8_t engine, preset;
    step8_t step[PROJ_V6_STEPS];
} proj_trk_v3_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[PROJ_NG_V3];
    uint8_t sel, rsv[3];
    proj_trk_v3_t t[PROJ_V6_TRACKS];
    uint32_t sum;
} project_v3_t;
typedef struct {
    int16_t p[PROJ_NP_V2];
    uint8_t engine, preset;
    step8_t step[PROJ_V6_STEPS];
} proj_trk_v2_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[PROJ_NG_V2];
    uint8_t sel, rsv[3];
    proj_trk_v2_t t[PROJ_V6_TRACKS];
    uint32_t sum;
} project_v2_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[PROJ_NG_V2];
    proj_trk_v2_t t;
    uint32_t sum;
} project_v1_t;

_Static_assert(sizeof(project_v2_t) == 2552u && sizeof(project_v1_t) == 688u &&
               sizeof(project_v3_t) == 2584u && sizeof(project_v4_t) == 3112u &&
               sizeof(project_v5_t) == 3840u,
               "FUN1-FUN5 wire layouts must remain byte-for-byte compatible");

/* Accessors for the split-parameter FUN6 representation. */
static int16_t pt_get(const proj_trk_t *t, uint32_t id)
{
    return id < PROJ_NC6 ? t->p[id] :
           id < P_E0 ? (int16_t)t->x[id - PROJ_NC6] :
           t->e[(id - P_E0) & 7u];
}
UI_SMALL static void pt_set(proj_trk_t *t, uint32_t id, int32_t v)
{
    if (id < PROJ_NC6)
        t->p[id] = (int16_t)v;
    else if (id < P_E0)
        t->x[id - PROJ_NC6] = (uint8_t)clamp(v, 0, 255);
    else
        t->e[(id - P_E0) & 7u] = (int16_t)v;
}
static int32_t pt_micro(const proj_trk_t *t, uint32_t i)
{
    int32_t m = t->mf[i] & 63;
    return m >= 32 ? m - 64 : m;
}
static uint32_t pt_fill(const proj_trk_t *t, uint32_t i) { return (uint32_t)t->mf[i] >> 6; }
UI_SMALL static void pt_set_mf(proj_trk_t *t, uint32_t i, int32_t micro, uint32_t fill)
{
    t->mf[i] = (uint8_t)(((uint32_t)micro & 63u) | (fill & 3u) << 6);
}

/* Shared working-project snapshot: used by both the FM-1 autosave path and
 * System 37's catalogue-backed working slot. Keep it in POOL on both boards. */
static project_t autosave_buf __attribute__((section(".pool")));
#if BOARD_SMK37
/* Eight FUN7 caches are split across ordinary RAM, LOWRAM and NOINIT: five,
 * two and one respectively. The core-1 stack is in POOL; the song-return
 * snapshot shares autosave_buf, and import scratch shares the no-init
 * catalogue scratch. The catalogue is persistent, so caches are repopulated
 * from flash at boot. */
static project_t proj_slot_ram[5];
static project_t proj_slot_low[2] __attribute__((section(".lowram")));
static project_t proj_slot_noinit[1] __attribute__((section(".noinit")));
static project_t *const proj_slot[8] = {
    &proj_slot_ram[0], &proj_slot_ram[1], &proj_slot_ram[2], &proj_slot_ram[3],
    &proj_slot_ram[4], &proj_slot_low[0], &proj_slot_low[1], &proj_slot_noinit[0]
};
#else
static project_t proj_slot_fm1[4] __attribute__((section(".noinit")));
static project_t *const proj_slot[4] = {
    &proj_slot_fm1[0], &proj_slot_fm1[1], &proj_slot_fm1[2], &proj_slot_fm1[3]
};
#endif
#if BOARD_SMK37
static uint8_t s37_cat_ready, s37_migrate_pending, s37_cat_uncertain;
/* Catalogue objects are used by the boot/migration routines above their
 * initialised definitions below; keep their declarations visible first. */
static const sm_map_t s37_cat_map;
static sm_state_t s37_cat_state;
static s37_cat_header_t s37_cat_header;
/* Import staging and catalogue validation share a no-init union. They are
 * used in separate phases; the eight persistent section caches remain intact. */
static union {
    project_v7_t v7;
    project_v6_legacy_t v6;
    project_v5_t v5;
    project_v4_t v4;
    project_v3_t v3;
    project_v2_t v2;
    project_v1_t v1;
} s37_scratch __attribute__((section(".noinit")));
#define s37_cat_scratch s37_scratch.v7
#define proj_tmp s37_scratch
static s37_cat_overlay_t s37_cat_overlay;
static s37_cat_image_t s37_cat_image;
static const sm_io_t s37_cat_io;
#if BOARD_SMK37
static uint8_t backup_stage_locked;
static uint32_t backup_stage_ms;
void *project_backup_stage(void) { return &autosave_buf; }
void project_backup_stage_lock(void) { backup_stage_locked = 1u; backup_stage_ms = fm1_ms; }
void project_backup_stage_touch(void) { if (backup_stage_locked) backup_stage_ms = fm1_ms; }
void project_backup_stage_unlock(void) { backup_stage_locked = 0u; }
int project_backup_stage_active(void)
{
    if (!backup_stage_locked)
        return 0;
    if ((uint32_t)(fm1_ms - backup_stage_ms) > 120000u) {
        backup_stage_locked = 0u;
        return 0;
    }
    return 1;
}
#endif
static void s37_catalog_boot(void);
static int s37_catalog_load_section(uint32_t slot, project_t *p);
#endif

UI_SMALL static uint32_t proj_hash(const void *p, uint32_t n)   /* FNV-1a over n bytes */
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t i, s = 0x811C9DC5u;
    for (i = 0; i < n; i++)
        s = (s ^ b[i]) * 16777619u;
    return s;
}
UI_SMALL static uint32_t proj_sum(const project_v6_legacy_t *p) { return proj_hash(p, sizeof *p - 4u); }
UI_SMALL static int proj_ok(const project_v6_legacy_t *q) { return q->magic == PROJ_MAGIC && q->size == sizeof *q && q->sum == proj_sum(q); }
static int project_valid(const project_t *q)
{
#if BOARD_SMK37
    return proj_v7_valid(q);
#else
    return proj_ok(q);
#endif
}

/* ---- old formats -> format 5 */
/* an old step into a synth step (no level, no ratchet) */
UI_SMALL static void step_from8(step_t *d, const step8_t *s)
{
    memcpy(d->note, s->note, 4);
    d->n = s->n;
    d->time = s->time;
    d->flags = s->flags;
    d->vel = s->vel;
    d->lvl = d->rat = 0;
}
/* an old drum step (GM notes) into the drum track's lanes; its velocity / accent -> their level */
UI_SMALL static void dstep_from8(dstep_t *d, const step8_t *s)
{
    uint32_t i, lvl = (s->flags & SF_ACCENT) || s->vel > 115u ? LV_HARD : !s->vel ? LV_NORM : vel_lvl(s->vel);
    memset(d, 0, sizeof *d);
    if (s->time != ST_NOTE)
        return;
    for (i = 0; i < s->n && i < 4u; i++)
        dstep_set(d, lane_of_note(s->note[i] & 127u), lvl, 0);
}
UI_SMALL static int16_t swing_from_v3(int32_t v) { return (int16_t)clamp((v * 4 + 2) / 5, 0, 100); }   /* /250 -> /200 */

/* the globals of formats 1..3 (G_* unchanged since; any added later: their defaults) */
UI_SMALL static void proj_g_from_old(int16_t *g, const int16_t *g2)
{
    uint32_t i;
    for (i = 0; i < PROJ_NG; i++)
        g[i] = i < PROJ_NG_V3 ? g2[i] : GP[i].def;
    g[G_SWING] = swing_from_v3(g[G_SWING]);
}

/* no nudge, no lock, no condition (formats 1..4); the 2.6 parameters at their defaults (formats 1..5) */
UI_SMALL static void proj_trk_plain(proj_trk_t *d)
{
    uint32_t k;
    memset(d->mf, 0, sizeof d->mf);
    for (k = PROJ_NC6; k < P_E0; k++)
        pt_set(d, k, TP[k].def);
    for (k = 0; k < PROJ_V6_LOCKS; k++) {
        d->lock[k].step = LOCK_FREE;
        d->lock[k].param = 0;
        d->lock[k].val = 0;
    }
}

/* a track of format 3 -> today's (by id up to P_SLDEPTH; P_E0.. moved) */
UI_SMALL static void proj_trk_from_v3(proj_trk_t *d, const proj_trk_v3_t *s, int drum)
{
    uint32_t k, nc = PROJ_NP_V3 - 8u;
    proj_trk_plain(d);
    for (k = 0; k < PROJ_NC6; k++)
        d->p[k] = k < nc ? s->p[k] : TP[k].def;
    for (k = 0; k < 8u; k++)
        d->e[k] = s->p[nc + k];
    d->p[P_SSWING] = swing_from_v3(d->p[P_SSWING]);
    d->p[P_ASWING] = swing_from_v3(d->p[P_ASWING]);
    d->engine = drum ? 0u : s->engine;
    d->preset = drum ? 0u : s->preset;
    for (k = 0; k < NSTEP; k++) {
        if (drum)
            dstep_from8(&d->dstep[k], &s->step[k]);
        else
            step_from8(&d->step[k], &s->step[k]);
    }
}

/* a track of formats 1 and 2 -> format 3 (mapped by count, see the top) */
UI_SMALL static void proj_trk_v2_to_v3(proj_trk_v3_t *d, const proj_trk_v2_t *s, int drum)
{
    uint32_t k, nc = PROJ_NP_V2 - 8u;
    for (k = 0; k < PROJ_NP_V3 - 8u; k++)
        d->p[k] = k < nc ? s->p[k] : TP[k].def;
    for (k = 0; k < 8u; k++)
        d->p[PROJ_NP_V3 - 8u + k] = s->p[nc + k];
    d->engine = drum ? 0u : s->engine;          /* (indices 0..7 as they were) */
    d->preset = drum ? 0u : s->preset;
    memcpy(d->step, s->step, sizeof d->step);
}

/* a format 4 project (n bytes in *v4) -> slot q as format 6: the same, no nudge, no lock, no condition, no FILTER */
UI_SMALL static int proj_from_v4(project_v6_legacy_t *q, const project_v4_t *v4, int n)
{
    uint32_t i;
    if (n != (int)sizeof *v4 || v4->magic != PROJ_MAGIC_V4 || v4->size != sizeof *v4 ||
        v4->sum != proj_hash(v4, sizeof *v4 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    memcpy(q->g, v4->g, sizeof q->g);
    q->sel = v4->sel;
    for (i = 0; i < PROJ_V6_TRACKS; i++) {
        uint32_t k;
        proj_trk_plain(&q->t[i]);
        for (k = 0; k < PROJ_NC6; k++)                 /* by id up to P_CHORD, the parameters added since: */
            q->t[i].p[k] = k < PROJ_NP_V4 - 8u ? v4->t[i].p[k] : TP[k].def;   /* their defaults; P_E0.. moved */
        for (k = 0; k < 8u; k++)
            q->t[i].e[k] = v4->t[i].p[PROJ_NP_V4 - 8u + k];
        q->t[i].engine = v4->t[i].engine;
        q->t[i].preset = v4->t[i].preset;
        memcpy(q->t[i].step, v4->t[i].step, sizeof q->t[i].step);
    }
    q->sum = proj_sum(q);
    return 1;
}

/* a format 5 project (n bytes in *v5) -> slot q as format 6: by id, the 2.6 parameters at their defaults, each step's
 * nudge and condition into its byte, the locks as they were (a lock on P_E0.. follows its parameter's new id) */
UI_SMALL static int proj_from_v5(project_v6_legacy_t *q, const project_v5_t *v5, int n)
{
    uint32_t i, k;
    if (n != (int)sizeof *v5 || v5->magic != PROJ_MAGIC_V5 || v5->size != sizeof *v5 ||
        v5->sum != proj_hash(v5, sizeof *v5 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    memcpy(q->g, v5->g, sizeof q->g);
    q->sel = v5->sel;
    q->drdly = v5->drdly;
    for (i = 0; i < PROJ_V6_TRACKS; i++) {
        const proj_trk_v5_t *s = &v5->t[i];
        proj_trk_t *d = &q->t[i];
        proj_trk_plain(d);
        for (k = 0; k < PROJ_NC6; k++)
            d->p[k] = s->p[k];
        for (k = 0; k < 8u; k++)
            d->e[k] = s->p[PROJ_NP_V5 - 8u + k];
        d->engine = s->engine;
        d->preset = s->preset;
        memcpy(d->step, s->step, sizeof d->step);
        for (k = 0; k < NSTEP; k++)
            pt_set_mf(d, k, clamp(s->micro[k], MICRO_MIN, MICRO_MAX), (uint32_t)s->fill[k / 4u] >> (2u * (k % 4u)));
        for (k = 0; k < PROJ_V6_LOCKS; k++) {
            d->lock[k] = s->lock[k];
            if (s->lock[k].step != LOCK_FREE && s->lock[k].param >= PROJ_NP_V5 - 8u)   /* P_E0.. of format 5 */
                d->lock[k].param = (uint8_t)(s->lock[k].param - (PROJ_NP_V5 - 8u) + P_E0);
        }
    }
    q->sum = proj_sum(q);
    return 1;
}

/* a format 3 project -> slot q as format 6 */
UI_SMALL static void proj_from_v3_ok(project_v6_legacy_t *q, const project_v3_t *v3)
{
    uint32_t i;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    proj_g_from_old(q->g, v3->g);
    q->sel = v3->sel;
    for (i = 0; i < PROJ_V6_TRACKS; i++)
        proj_trk_from_v3(&q->t[i], &v3->t[i], i == PROJ_V6_DRUM_TRACK);
    q->sum = proj_sum(q);
}
UI_SMALL static int proj_from_v3(project_v6_legacy_t *q, const project_v3_t *v3, int n)
{
    if (n != (int)sizeof *v3 || v3->magic != PROJ_MAGIC_V3 || v3->size != sizeof *v3 ||
        v3->sum != proj_hash(v3, sizeof *v3 - 4u))
        return 0;
    proj_from_v3_ok(q, v3);
    return 1;
}

static project_v3_t proj_v3_tmp;               /* (formats 1, 2: through format 3) */
/* a format 2 project (n bytes in *v2) -> slot q as format 5 */
UI_SMALL static int proj_from_v2(project_v6_legacy_t *q, const project_v2_t *v2, int n)
{
    project_v3_t *v3 = &proj_v3_tmp;
    uint32_t i;
    if (n != (int)sizeof *v2 || v2->magic != PROJ_MAGIC_V2 || v2->size != sizeof *v2 ||
        v2->sum != proj_hash(v2, sizeof *v2 - 4u))
        return 0;
    memset(v3, 0, sizeof *v3);
    memcpy(v3->g, v2->g, sizeof v3->g);
    v3->sel = v2->sel;
    for (i = 0; i < PROJ_V6_TRACKS; i++)
        proj_trk_v2_to_v3(&v3->t[i], &v2->t[i], i == PROJ_V6_DRUM_TRACK);
    proj_from_v3_ok(q, v3);
    return 1;
}

/* a format 1 project (n bytes in *v1) -> slot q as format 5: the instrument becomes track 1,
 * tracks 2..4 start empty (their sounds as at power-on) */
UI_SMALL static int proj_from_v1(project_v6_legacy_t *q, const project_v1_t *v1, int n)
{
    project_v3_t *v3 = &proj_v3_tmp;
    uint32_t i;
    if (n != (int)sizeof *v1 || v1->magic != PROJ_MAGIC_V1 || v1->size != sizeof *v1 ||
        v1->sum != proj_hash(v1, sizeof *v1 - 4u))
        return 0;
    memset(v3, 0, sizeof *v3);
    memcpy(v3->g, v1->g, sizeof v3->g);
    proj_trk_v2_to_v3(&v3->t[0], &v1->t, 0);
    proj_from_v3_ok(q, v3);
    for (i = 1; i < PROJ_V6_TRACKS; i++) {               /* the other tracks: their defaults, no steps */
        uint32_t k;
        for (k = 0; k < P_COUNT; k++)
            pt_set(&q->t[i], k, k >= P_E0 ? ENGINES[trk_def_engine(i)]->edit[k - P_E0].def : TP[k].def);
        q->t[i].engine = (uint8_t)trk_def_engine(i);
        q->t[i].preset = 0xFF;                 /* 0xFF: its default preset (project_load) */
        memset(q->t[i].step, 0, sizeof q->t[i].step);
        if (i != PROJ_V6_DRUM_TRACK)
            for (k = 0; k < NSTEP; k++)
                q->t[i].step[k].time = ST_REST;
        proj_trk_plain(&q->t[i]);
    }
    q->sum = proj_sum(q);
    return 1;
}

/* n bytes of a stored project (any format) -> slot q as format 5; 0 = not a project */
UI_SMALL static int proj_import(project_v6_legacy_t *q, const void *b, int n)
{
    if (n == (int)sizeof *q && proj_ok((const project_v6_legacy_t *)b)) {
        memcpy(q, b, sizeof *q);
        return 1;
    }
    return proj_from_v5(q, (const project_v5_t *)b, n) || proj_from_v4(q, (const project_v4_t *)b, n) ||
           proj_from_v3(q, (const project_v3_t *)b, n) ||
           proj_from_v2(q, (const project_v2_t *)b, n) || proj_from_v1(q, (const project_v1_t *)b, n);
}

/* ---- the working project <-> a project_t */
/* 2.6: GLO > TEXTURE in a project (a song section has its own): in the slots of two globals a project no longer
 * applies (G_DRCH: a setting of the FM-1 now; G_INFO: the CPU readout) and the spare bytes. rsv[0]: 0x80 (there) | SRC
 * | WHEN << 4 | DUCK << 5, rsv[1]: RATE, g[G_DRCH]: LEVEL | (TONE + 64) << 7, g[G_INFO]: MOVE. Without the 0x80 (a
 * project before it, converted): the defaults, OFF */
UI_SMALL static void proj_tx_put(project_t *p)
{
    p->rsv[0] = (uint8_t)(0x80u | ((uint32_t)song.g[G_TXSRC] & 15u) | ((uint32_t)song.g[G_TXWHEN] & 1u) << 4 |
                          ((uint32_t)song.g[G_TXDUCK] & 1u) << 5 | 0x40u);   /* (0x40: saved by 2.7 or later) */
    p->rsv[1] = (uint8_t)song.g[G_TXRATE];
    p->g[G_DRCH] = (int16_t)((song.g[G_TXLVL] & 127) | ((song.g[G_TXTONE] + 64) & 127) << 7);
    p->g[G_INFO] = song.g[G_TXMOVE];
}
static void proj_tx_get(const project_t *p)
{
    static const uint8_t ID[7] = {G_TXSRC, G_TXLVL, G_TXTONE, G_TXMOVE, G_TXRATE, G_TXWHEN, G_TXDUCK};
    int32_t v[7];
    uint32_t i;
    if (!(p->rsv[0] & 0x80u)) {
        for (i = 0; i < 7u; i++)
            v[i] = GP[ID[i]].def;
    } else {
        v[0] = p->rsv[0] & 15u, v[5] = (p->rsv[0] >> 4) & 1u, v[6] = (p->rsv[0] >> 5) & 1u, v[4] = p->rsv[1];
        v[1] = p->g[G_DRCH] & 127, v[2] = ((p->g[G_DRCH] >> 7) & 127) - 64, v[3] = p->g[G_INFO];
    }
    for (i = 0; i < 7u; i++)
        song.g[ID[i]] = (int16_t)clamp(v[i], GP[ID[i]].min, GP[ID[i]].max);
}

/* (2.7) the DRUMS screen's SOUND page in a project: each sound's TUNE (4 bits) and DECAY (3 bits), two's complement
 * (0: as the kit has it), 16 x 7 bits in the drum track's engine values E1..E7 (the drum track has no engine: they
 * were free), and a check of them in g[G_SAVE] (never loaded: SAVE is a GO button). An older SLOOP keeps neither
 * (it limits E1..E7 to an engine's ranges and saves G_SAVE as 0): its project opens with every sound as the kit has it */
#define DRUM_TW_MARK 0x5400
UI_SMALL static uint32_t drum_tw_sum(const int16_t *e)
{
    uint32_t i, s = 0x5Au;
    for (i = 1; i < 8u; i++)
        s += ((uint32_t)(uint16_t)e[i] & 0xFFu) * (i * 2u + 1u) + ((uint32_t)(uint16_t)e[i] >> 8) * (i * 2u + 2u);
    return s & 0xFFu;
}
UI_SMALL static void drum_tw_put(project_t *p)
{
    uint16_t w[7] = {0};
    uint32_t l, b;
    for (l = 0; l < DRUM_LANES; l++) {
        uint32_t v = ((uint32_t)drum_tune[l] & 15u) | ((uint32_t)drum_dec[l] & 7u) << 4;
        for (b = 0; b < 7u; b++)
            if ((v >> b) & 1u)
                w[(l * 7u + b) / 16u] |= (uint16_t)(1u << ((l * 7u + b) % 16u));
    }
    for (l = 0; l < 7u; l++)
        p->t[PROJ_LIVE_DRUM_TRACK].e[l + 1u] = (int16_t)w[l];
    p->g[G_SAVE] = (int16_t)(DRUM_TW_MARK | drum_tw_sum(p->t[PROJ_LIVE_DRUM_TRACK].e));
}
static void drum_tw_get(const project_t *p)
{
    const int16_t *e = p->t[PROJ_LIVE_DRUM_TRACK].e;
    uint32_t l, b, ok = (uint16_t)p->g[G_SAVE] == (DRUM_TW_MARK | drum_tw_sum(e));
    for (l = 0; l < DRUM_LANES; l++) {
        uint32_t v = 0;
        for (b = 0; ok && b < 7u; b++)
            if (((uint16_t)e[1u + (l * 7u + b) / 16u] >> ((l * 7u + b) % 16u)) & 1u)
                v |= 1u << b;
        drum_tune[l] = (int8_t)((int32_t)((v & 15u) ^ 8u) - 8);
        drum_dec[l] = (int8_t)((int32_t)(((v >> 4) & 7u) ^ 4u) - 4);
    }
}

#if BOARD_SMK37
typedef struct { const project_v7_t *image; project_v7_t *capture; } proj_v7_apply_context_t;
static proj_v7_apply_context_t proj_v7_apply_context;

static int proj_v7_capture_globals(void *context, project_v7_t *out)
{
    uint32_t i;
    (void)context;
    for (i = 0; i < PROJ_V7_GLOBALS; i++)
        out->g[i] = song.g[i];
    out->sel = song.sel;
    out->drdly = (uint8_t)song.g[G_DRDLY];
    proj_tx_put(out);
    return 0;
}
static int proj_v7_capture_track(void *context, uint8_t index, proj_trk_v7_t *out)
{
    const track_t *t;
    uint32_t i;
    if (index >= NTRK)
        return -1;
    t = &trk[index];
    for (i = 0; i < PROJ_V6_NC; i++)
        out->p[i] = (int8_t)clamp(t->p[i], -128, 127);
    for (i = 0; i < 8u; i++)
        out->e[i] = t->p[P_E0 + i];
    for (i = 0; i < PROJ_V6_NX; i++)
        out->x[i] = (uint8_t)t->p[PROJ_V6_NC + i];
    out->engine = index < NPART ? t->eng_req : 0u;
    out->preset = t->preset;
    memcpy(out->step, t->step, sizeof out->step);
    memcpy(out->fill, t->fill, sizeof out->fill);
    for (i = 0; i < NSTEP; i++)
        proj_v7_micro_set(out, i, t->micro[i]);
    memcpy(out->lock, t->lock, sizeof out->lock);
    if (index == TRK_DRUM)
        drum_tw_put(((proj_v7_apply_context_t *)context)->capture);
    return 0;
}
static int proj_v7_validate_globals(void *context, const project_v7_t *in, int all)
{
    (void)context;
    (void)all;
    return in->sel < NTRK ? 0 : -1;
}
static int proj_v7_validate_track(void *context, uint8_t index, const proj_trk_v7_t *in)
{
    (void)context;
    (void)in;
    return index < NTRK ? 0 : -1; /* unsupported legacy engines are safely substituted during apply */
}
static void proj_v7_apply_globals(void *context, const project_v7_t *in, int all)
{
    uint32_t i;
    (void)context;
    for (i = 0; i < PROJ_V7_GLOBALS; i++)
        if (all ? i != G_SLOT && i != G_LOAD && i != G_SAVE && i != G_SYNC && i != G_MIDI &&
                      i != G_ROUTE && i != G_DRCH && i != G_INFO
                : i == G_DRLVL || i == G_DRREV)
            song.g[i] = (int16_t)clamp(in->g[i], GP[i].min, GP[i].max);
    song.g[G_DRDLY] = (int16_t)clamp(in->drdly, GP[G_DRDLY].min, GP[G_DRDLY].max);
    proj_tx_get(in);
    drum_tw_get(in);
}
static void proj_v7_apply_track(void *context, uint8_t index, const proj_trk_v7_t *s)
{
    track_t *t;
    uint32_t i, e;
    if (index >= NTRK)
        return;
    t = &trk[index];
    e = index < NPART ? (s->engine < NENGINES ? s->engine : trk_def_engine(index)) : 0u;
    t->eng_req = (uint8_t)e;
    t->user = 0;
    t->lk_n = 0;
    for (i = 0; i < P_COUNT; i++) {
        int32_t value;
        const param_desc_t *d;
        if (i < PROJ_V6_NC)
            value = s->p[i];
        else if (i < PROJ_V6_NC + PROJ_V6_NX)
            value = s->x[i - PROJ_V6_NC];
        else
            value = s->e[i - PROJ_V6_NC - PROJ_V6_NX];
        d = index == TRK_DRUM && i == P_E0 ? &DRUM_KIT_DESC :
            i >= P_E0 && i <= P_E7 ? &ENGINES[e]->edit[i - P_E0] : &TP[i];
        t->p[i] = (int16_t)clamp(value, d->min, d->max);
    }
    if (!(((proj_v7_apply_context_t *)context)->image->rsv[0] & 0x40u) && !t->p[P_AMODE])
        t->p[P_AHOLD] = 0;
    t->preset = (uint8_t)(ENGINES[e]->npresets ?
        (s->preset == 0xFFu ? 0u : s->preset) % ENGINES[e]->npresets : 0u);
    memcpy(t->step, s->step, sizeof t->step);
    memcpy(t->fill, s->fill, sizeof t->fill);
    for (i = 0; i < NSTEP; i++)
        t->micro[i] = (int8_t)proj_v7_micro_get(s, i);
    for (i = 0; i < PROJ_V7_LOCKS; i++) {
        const plock_t *l = &s->lock[i];
        uint32_t id = l->param;
        if (l->step < NSTEP && id < P_COUNT && p_lockable(id)) {
            const param_desc_t *d = index == TRK_DRUM && id == P_E0 ? &DRUM_KIT_DESC :
                id >= P_E0 && id <= P_E7 ? &ENGINES[e]->edit[id - P_E0] : &TP[id];
            t->lock[i].step = l->step;
            t->lock[i].param = (uint8_t)id;
            t->lock[i].val = (int16_t)clamp(l->val, d->min, d->max);
        } else {
            t->lock[i].step = LOCK_FREE;
            t->lock[i].param = 0;
            t->lock[i].val = 0;
        }
    }
    if (index != TRK_DRUM)
        for (i = 0; i < NSTEP; i++) {
            step_t *st = &t->step[i];
            uint32_t j;
            if (st->n > 4u) st->n = 4u;
            if (st->time > ST_REST) st->time = ST_REST;
            st->vel &= 127u;
            for (j = 0; j < 4u; j++)
                st->note[j] &= 127u;
        }
    fm6_track_loaded(t);
}
static const s37_project_v7_live_ops_t proj_v7_live_ops = {
    .context = &proj_v7_apply_context,
    .capture_globals = proj_v7_capture_globals,
    .capture_track = proj_v7_capture_track,
    .validate_globals = proj_v7_validate_globals,
    .validate_track = proj_v7_validate_track,
    .apply_globals = proj_v7_apply_globals,
    .apply_track = proj_v7_apply_track
};
UI_SMALL static void proj_capture(project_t *p)
{
    proj_v7_apply_context.capture = p;
    if (s37_project_v7_capture(p, &proj_v7_live_ops))
        memset(p, 0, sizeof *p);
    proj_v7_apply_context.capture = 0;
}
static void proj_apply(const project_t *p, int all)
{
    proj_v7_apply_context.image = p;
    (void)s37_project_v7_apply(p, &proj_v7_live_ops, all);
    proj_v7_apply_context.image = 0;
}
#else
UI_SMALL static void proj_capture(project_t *p)        /* the original FM-1 FUN6 project */
{
    uint32_t i;
    memset(p, 0, sizeof *p);
    p->magic = PROJ_MAGIC;
    p->size = sizeof *p;
    for (i = 0; i < PROJ_NG; i++)
        p->g[i] = song.g[i];
    p->sel = song.sel;
    p->drdly = (uint8_t)song.g[G_DRDLY];
    proj_tx_put(p);
    for (i = 0; i < PROJ_V6_TRACKS; i++) {
        uint32_t k;
        for (k = 0; k < P_COUNT; k++)
            pt_set(&p->t[i], k, trk[i].p[k]);
        p->t[i].engine = trk[i].eng_req;
        p->t[i].preset = trk[i].preset;
        memcpy(p->t[i].step, trk[i].step, sizeof trk[i].step);
        for (k = 0; k < NSTEP; k++)
            pt_set_mf(&p->t[i], k, trk[i].micro[k], (uint32_t)trk[i].fill[k / 4u] >> (2u * (k % 4u)));
        memcpy(p->t[i].lock, trk[i].lock, sizeof trk[i].lock);
    }
    drum_tw_put(p);
    p->sum = proj_sum(p);
}
static void proj_apply(const project_t *p, int all)
{
    uint32_t i, k;
    for (i = 0; i < PROJ_NG; i++)
        if (all ? i != G_SLOT && i != G_LOAD && i != G_SAVE && i != G_SYNC && i != G_MIDI && i != G_ROUTE && i != G_DRCH &&
                      i != G_INFO
                : i == G_DRLVL || i == G_DRREV)
            song.g[i] = (int16_t)clamp(p->g[i], GP[i].min, GP[i].max);
    song.g[G_DRDLY] = (int16_t)clamp(p->drdly, GP[G_DRDLY].min, GP[G_DRDLY].max);
    proj_tx_get(p);
    drum_tw_get(p);
    for (k = 0; k < PROJ_V6_TRACKS; k++) {
        track_t *t = &trk[k];
        const proj_trk_t *s = &p->t[k];
        uint32_t e = k < PROJ_V6_PARTS ? s->engine % NENGINES : 0u;
        t->eng_req = (uint8_t)e;
        t->user = 0;
        t->lk_n = 0;
        for (i = 0; i < P_COUNT; i++) {
            const param_desc_t *d = k == PROJ_V6_DRUM_TRACK && i == P_E0 ? &DRUM_KIT_DESC :
                                    i >= P_E0 && i <= P_E7 ? &ENGINES[e]->edit[i - P_E0] : &TP[i];
            t->p[i] = (int16_t)clamp(pt_get(s, i), d->min, d->max);
        }
        if (!(p->rsv[0] & 0x40u) && !t->p[P_AMODE])
            t->p[P_AHOLD] = 0;
        t->preset = (uint8_t)(ENGINES[e]->npresets ? (s->preset == 0xFFu ? 0u : s->preset) % ENGINES[e]->npresets : 0u);
        memcpy(t->step, s->step, sizeof t->step);
        memset(t->fill, 0, sizeof t->fill);
        for (i = 0; i < NSTEP; i++) {
            uint32_t c = pt_fill(s, i);
            t->micro[i] = (int8_t)clamp(pt_micro(s, i), MICRO_MIN, MICRO_MAX);
            if (c != 3u)
                t->fill[i / 4u] |= (uint8_t)(c << (2u * (i % 4u)));
        }
        for (i = 0; i < PROJ_V6_LOCKS; i++) {
            const plock_t *l = &s->lock[i];
            uint32_t id = l->param;
            if (l->step < NSTEP && id < P_COUNT && p_lockable(id)) {
                const param_desc_t *d = k == PROJ_V6_DRUM_TRACK && id == P_E0 ? &DRUM_KIT_DESC :
                                        id >= P_E0 && id <= P_E7 ? &ENGINES[e]->edit[id - P_E0] : &TP[id];
                t->lock[i].step = l->step;
                t->lock[i].param = (uint8_t)id;
                t->lock[i].val = (int16_t)clamp(l->val, d->min, d->max);
            } else {
                t->lock[i].step = LOCK_FREE;
                t->lock[i].param = 0;
                t->lock[i].val = 0;
            }
        }
        if (k != PROJ_V6_DRUM_TRACK)
            for (i = 0; i < NSTEP; i++) {
                step_t *st = &t->step[i];
                uint32_t j;
                if (st->n > 4u) st->n = 4u;
                if (st->time > ST_REST) st->time = ST_REST;
                st->vel &= 127u;
                for (j = 0; j < 4u; j++) st->note[j] &= 127u;
            }
        fm6_track_loaded(t);
    }
}
#endif

#ifndef PROJ_HOST
#if FELUCCA_ARRANGER
#include "arranger_scene.c"
#endif
static uint8_t sec_dirty, song_dirty;           /* live sections / the song: in RAM, not yet in flash */
#if FELUCCA_FLASH
/* Legacy one-sector objects remain readable until the first verified FUN7 catalogue commit. */
#if !BOARD_SMK37
static union {
    project_v6_legacy_t v6;
    project_v5_t v5;
    project_v4_t v4;
    project_v3_t v3;
    project_v2_t v2;
    project_v1_t v1;
} proj_tmp;
#endif
UI_SMALL static void *up_stage(void) { return &proj_tmp; }   /* upreset.c: a user preset bank staged for a write */
UI_SMALL static void proj_fetch(uint32_t slot)
{
#if BOARD_SMK37
    project_t *q = proj_slot[slot & 7u];
    int n;
    uint8_t loss = 0;
    if (s37_cat_ready) {
        if (s37_catalog_load_section(slot & 7u, q))
            memset(q, 0, sizeof *q);
        return;
    }
    n = slot < 4u ? st_load(OBJ_PROJECT0 + slot, &proj_tmp, sizeof proj_tmp) : 0;
    /* Slot H is the temporary FUN6 conversion image while the four legacy
     * objects are imported. It is empty on this pre-catalogue migration path. */
    project_v6_legacy_t *stage = (project_v6_legacy_t *)proj_slot[7];
    if (slot >= 4u || n <= 0 || !proj_import(stage, &proj_tmp, n) ||
        !proj_v6_to_v7(q, stage, sizeof *stage, &loss, TP, ENGINES[ENGI_ANALOG]->edit))
        memset(q, 0, sizeof *q);
#else
    project_t *q = proj_slot[slot & 3u];
    int n = st_load(OBJ_PROJECT0 + (slot & 3u), &proj_tmp, sizeof proj_tmp);
    if (!proj_import(q, &proj_tmp, n))
        q->magic = 0;
#endif
}
#if BOARD_SMK37
static int proj_import_v7(project_t *out, const void *raw, uint32_t length)
{
    uint8_t lock_loss = 0;
    if (!out || !raw || !length)
        return 0;
    if (length == sizeof(project_v7_t) && proj_v7_valid((const project_v7_t *)raw)) {
        memcpy(out, raw, sizeof *out);
        return 1;
    }
    if (!proj_import(&s37_scratch.v6, raw, (int)length))
        return 0;
    return proj_v6_to_v7(out, &s37_scratch.v6, sizeof s37_scratch.v6, &lock_loss,
                         TP, ENGINES[ENGI_ANALOG]->edit);
}
#endif
#include "fm6_bank.c"                           /* the FM6 patch bank (eng_fm6.c PTCH B1..B27): staged in proj_tmp */
#endif

static const char *const SEC_NAME[8] = {"A", "B", "C", "D", "E", "F", "G", "H"};   /* (2.7: the slots named as the song's sections) */
#if BOARD_SMK37
static uint8_t proj_save_err;                   /* SLOOP Pro: the last project_save could not write the flash (editor.c) */
#if BOARD_SMK37
static void s37_catalog_boot(void)
{
    uint32_t i;
    int rc = sm_scan(&s37_cat_io, &s37_cat_map, &s37_cat_state);
    s37_cat_ready = 0u;
    s37_migrate_pending = 0u;
    s37_cat_uncertain = 0u;
    if (rc == 0) {
        if (s37_cat_state.uncertain)
            s37_cat_uncertain = 1u;
        if (s37_cat_state.length == S37_CAT_PAYLOAD_SIZE &&
            s37_cat_validate(&s37_cat_io, &s37_cat_map, &s37_cat_state,
                             proj_slot[6], &s37_cat_header) == 0 &&
            s37_cat_header.working_sum != 0u) {
            memset(proj_slot[6], 0, sizeof *proj_slot[6]);
            for (i = 0; i < S37_CAT_SECTIONS; i++) {
                uint32_t warm = proj_v7_valid(proj_slot[i]);
                if (warm)
                    memcpy(&autosave_buf, proj_slot[i], sizeof autosave_buf);
                if (s37_cat_header.present_mask & (uint8_t)(1u << i)) {
                    if (s37_cat_load_section(&s37_cat_io, &s37_cat_map, &s37_cat_state,
                                             (uint8_t)i, proj_slot[i], 0))
                        break;
                } else {
                    memset(proj_slot[i], 0, sizeof *proj_slot[i]);
                }
                if (warm && memcmp(&autosave_buf, proj_slot[i], sizeof autosave_buf)) {
                    memcpy(proj_slot[i], &autosave_buf, sizeof *proj_slot[i]);
                    sec_dirty |= (uint8_t)(1u << i);
                }
            }
            if (i == S37_CAT_SECTIONS &&
                s37_cat_load_working(&s37_cat_io, &s37_cat_map, &s37_cat_state, &autosave_buf) == 0) {
                s37_cat_ready = 1u;
                return;
            }
        }
        /* A storage-valid but semantically invalid catalogue is not safe to overwrite automatically. */
        s37_cat_uncertain = 1u;
        return;
    }
    if (rc == -2) {
        s37_cat_uncertain = 1u;
        return;
    }
    for (i = 0; i < S37_CAT_SECTIONS; i++) {
        uint32_t warm = proj_v7_valid(proj_slot[i]);
        if (warm)
            memcpy(&autosave_buf, proj_slot[i], sizeof autosave_buf);
        proj_fetch(i); /* imports the four legacy project slots; E-H start empty */
        if (warm && memcmp(&autosave_buf, proj_slot[i], sizeof autosave_buf)) {
            memcpy(proj_slot[i], &autosave_buf, sizeof *proj_slot[i]);
            sec_dirty |= (uint8_t)(1u << i);
        }
    }
    s37_migrate_pending = 1u;
}
#endif
#endif
#if BOARD_SMK37
static int s37_catalog_store_section(uint32_t slot, const project_t *p);
static int s37_catalog_erase_section(uint32_t slot);
static int s37_catalog_load_section(uint32_t slot, project_t *p);
#endif

UI_SMALL static void project_save(uint32_t slot)
{
#if BOARD_SMK37
    project_t *p = proj_slot[slot & 7u];
#else
    project_t *p = proj_slot[slot & 3u];
#endif
#if FELUCCA_ARRANGER
    if (song.playing || transport_req) { ui_message("STOP BEFORE SAVE"); return; }
#endif
    proj_capture(p);
#if FELUCCA_FLASH
    if (flash_ok) {
#if BOARD_SMK37
        proj_save_err = 0;
        if (s37_catalog_store_section(slot & 7u, p)) {
            proj_save_err = 1;
            if (s37_cat_ready)
                (void)s37_catalog_load_section(slot & 7u, p);
            else
                sec_dirty |= (uint8_t)(1u << (slot & 7u));
            ui_message("NOT SAVED: FLASH ERROR");
        } else {
            sec_dirty &= (uint8_t)~(1u << (slot & 7u));
            ui_say("SAVED SECTION ", SEC_NAME[slot & 7u]);
        }
#else
        if (st_save(OBJ_PROJECT0 + (slot & 3u), p, sizeof *p))
            ui_message("SAVE ERROR");
        else
            ui_say("SAVED SECTION ", SEC_NAME[slot & 3u]);
#endif
        return;
    }
#endif
#if BOARD_SMK37
    sec_dirty |= (uint8_t)(1u << (slot & 7u));
#endif
    ui_message("SAVED (RAM)");
}

/* Erase a section's directory entry; its old record remains unreachable until overwritten. */
UI_SMALL static void project_erase(uint32_t slot)
{
#if BOARD_SMK37
    slot &= 7u;
#else
    slot &= 3u;
#endif
#if FELUCCA_ARRANGER
    if (song.playing || transport_req) { ui_message("STOP BEFORE ERASE"); return; }
#endif
    if (!project_valid(proj_slot[slot])) {
        ui_say("EMPTY SECTION ", SEC_NAME[slot]);
        return;
    }
#if FELUCCA_FLASH
    if (flash_ok) {
#if BOARD_SMK37
        if (s37_catalog_erase_section(slot)) {
            ui_message("ERASE ERROR");
            return;
        }
#else
        if (st_save(OBJ_PROJECT0 + slot, proj_slot[slot], 0)) {
            ui_message("ERASE ERROR");
            return;
        }
#endif
    }
#endif
    fm1_irq_off();
    memset(proj_slot[slot], 0, sizeof *proj_slot[slot]);
    sec_dirty &= (uint8_t)~(1u << slot);
    if (live_sec == (int8_t)slot)
        live_sec = -1;
#if FELUCCA_ARRANGER
    if (arr_drop_scene(&arrangement, slot))
        song_dirty = 1;
#endif
    fm1_irq_on();
    ui_say("ERASED SECTION ", SEC_NAME[slot]);
}

/* Unsupported legacy engines are substituted with the track's safe default sound; pattern and mix survive. */
UI_SMALL static void proj_foreign_parts(const project_t *p)
{
    uint32_t k;
    for (k = 0; k < NPART; k++)
        if (p->t[k].preset != 0xFFu && p->t[k].engine >= NENGINES) {
            set_engine_of(&trk[k], TRK_DEF[k][0]);
            apply_preset_to(&trk[k], TRK_DEF[k][1]);
        }
}

/* A complete project into the working state. The audio ISR must not see a partial apply. */
UI_SMALL static void project_apply(const project_t *p)
{
    uint32_t k;
    transport_req = 2;
    panic_req = (1u << NTRK) - 1u;
    drum_mute = 0;
    for (k = 0; k < NTRK; k++)
        ex_off[k] = 0;
    fm1_irq_off();
    proj_apply(p, 1);
    song.sel = (uint8_t)(p->sel < NTRK ? p->sel : 0u);
    fm1_irq_on();
    for (k = 0; k < NPART; k++)
        if (p->t[k].preset == 0xFFu) {
            apply_preset_to(&trk[k], TRK_DEF[k][1]);
            steps_clear(&trk[k]);
        }
    proj_foreign_parts(p);
    sync_reload = 1;
    ui.force = 1;
}

UI_SMALL static void project_load(uint32_t slot)
{
#if BOARD_SMK37
    project_t *p = proj_slot[slot & 7u];
#else
    project_t *p = proj_slot[slot & 3u];
#endif
#if FELUCCA_ARRANGER
    if (song.playing || transport_req) { ui_message("STOP BEFORE LOAD"); return; }
#endif
#if FELUCCA_FLASH
    if (flash_ok && !project_valid(p))
        proj_fetch(slot);
#endif
    if (!project_valid(p)) {
        ui_say("EMPTY SECTION ", SEC_NAME[slot & (BOARD_SMK37 ? 7u : 3u)]);
        return;
    }
    project_apply(p);
    ui_say("LOADED SECTION ", SEC_NAME[slot & (BOARD_SMK37 ? 7u : 3u)]);
}

/* ---- the working project, kept in flash by itself: saved when it changed, the transport is stopped,
 * nothing sounds and the panel was not touched for AUTOSAVE_IDLE (a flash erase stops the audio for
 * ~50 ms: never while something plays); loaded at power-on (autosave_resume) */
#define AUTOSAVE_IDLE 2500u                    /* ms without input */
#define AUTOSAVE_GAP 20000u                    /* ms between two saves at least */


#if BOARD_SMK37
/* System 37 catalogue: two independent 12-sector copies. All four sample slots,
 * the FM6 bank, user presets, DAW settings and stock-reserved flash remain untouched. */
static const sm_map_t s37_cat_map = {
    .object_id = 0x3738u,
    .max_length = S37_CAT_PAYLOAD_SIZE,
    .sectors = {
        {0xEB000u,0xEC000u,0xED000u,0xEE000u,0xEF000u,0xF0000u,
         0xF1000u,0xF2000u,0xF3000u,0xFE000u,0xB7000u,0xB8000u},
        {0x9D000u,0x9E000u,0x9F000u,0xA0000u,0xA1000u,0xA2000u,
         0xA3000u,0xA4000u,0xA9000u,0xAA000u,0xB9000u,0xBA000u}
    },
    .sector_count = {12u,12u}
};

static int s37_flash_read(void *context, uint32_t offset, void *dst, uint32_t length)
{
    (void)context;
    return st_read(offset, dst, length);
}
static int s37_flash_erase(void *context, uint32_t offset)
{
    (void)context;
    return st_erase(offset);
}
static int s37_flash_program(void *context, uint32_t offset, const void *src, uint32_t length)
{
    (void)context;
    return st_prog(offset, src, length);
}
static const sm_io_t s37_cat_io = {
    .context = 0, .read = s37_flash_read, .erase = s37_flash_erase, .program = s37_flash_program
};

static uint8_t s37_catalog_default(uint8_t mask)
{
    uint32_t i, wanted = song.g[G_SLOT] > 0 ? (uint32_t)song.g[G_SLOT] - 1u : 0xFFu;
    if (wanted < S37_CAT_SECTIONS && (mask & (uint8_t)(1u << wanted)))
        return (uint8_t)wanted;
    for (i = 0; i < S37_CAT_SECTIONS; i++)
        if (mask & (uint8_t)(1u << i))
            return (uint8_t)i;
    return 0xFFu;
}

static int s37_catalog_write_full(void)
{
    const project_v7_t *sections[S37_CAT_SECTIONS];
    uint16_t bars[S37_CAT_SECTIONS];
    uint8_t mask = 0u, def;
    uint32_t i;
    if (s37_cat_uncertain)
        return -1;
    for (i = 0; i < S37_CAT_SECTIONS; i++) {
        if (project_valid(proj_slot[i])) {
            sections[i] = proj_slot[i];
            bars[i] = (uint16_t)section_bars(i);
            if (!bars[i]) bars[i] = 1u;
            mask |= (uint8_t)(1u << i);
        } else {
            sections[i] = 0;
            bars[i] = 0u;
        }
    }
    def = s37_catalog_default(mask);
    proj_capture(&autosave_buf);
    if (!proj_v7_valid(&autosave_buf) ||
        s37_cat_prepare_image(&s37_cat_image, sections, bars, mask, def, &autosave_buf) ||
        sm_save_stream(&s37_cat_io, &s37_cat_map, s37_cat_image_read, &s37_cat_image,
                       S37_CAT_PAYLOAD_SIZE, &s37_cat_state))
        return -1;
    s37_cat_header = s37_cat_image.header;
    s37_cat_ready = 1u;
    s37_migrate_pending = 0u;
    return 0;
}

static int s37_catalog_store_section(uint32_t slot, const project_t *p)
{
    uint16_t bars;
    uint8_t def;
    if (slot >= S37_CAT_SECTIONS || !p || !proj_v7_valid(p) || s37_cat_uncertain)
        return -1;
    if (!s37_cat_ready) {
        if (s37_catalog_write_full())
            return -1;
        return s37_catalog_store_section(slot, p);
    }
    bars = (uint16_t)section_bars(slot);
    if (!bars) bars = 1u;
    def = s37_cat_header.default_section;
    if (def >= S37_CAT_SECTIONS)
        def = (uint8_t)slot;
    if (s37_cat_prepare_overlay(&s37_cat_overlay, &s37_cat_io, &s37_cat_map,
                                &s37_cat_state, &s37_cat_scratch, (project_v7_t *)p,
                                (uint8_t)slot, bars, def) ||
        sm_save_stream(&s37_cat_io, &s37_cat_map, s37_cat_overlay_read,
                       &s37_cat_overlay, S37_CAT_PAYLOAD_SIZE, &s37_cat_state) ||
        s37_cat_validate(&s37_cat_io, &s37_cat_map, &s37_cat_state,
                         &s37_cat_scratch, &s37_cat_header))
        return -1;
    return 0;
}

static int s37_catalog_erase_section(uint32_t slot)
{
    if (slot >= S37_CAT_SECTIONS || s37_cat_uncertain)
        return -1;
    if (!s37_cat_ready) {
        if (s37_catalog_write_full())
            return -1;
        return s37_catalog_erase_section(slot);
    }
    if (s37_cat_prepare_erase_overlay(&s37_cat_overlay, &s37_cat_io, &s37_cat_map,
                                      &s37_cat_state, &s37_cat_scratch, (uint8_t)slot) ||
        sm_save_stream(&s37_cat_io, &s37_cat_map, s37_cat_overlay_read,
                       &s37_cat_overlay, S37_CAT_PAYLOAD_SIZE, &s37_cat_state) ||
        s37_cat_validate(&s37_cat_io, &s37_cat_map, &s37_cat_state,
                         &s37_cat_scratch, &s37_cat_header))
        return -1;
    return 0;
}

static int s37_catalog_load_section(uint32_t slot, project_t *p)
{
    if (!s37_cat_ready || slot >= S37_CAT_SECTIONS || !p)
        return -1;
    return s37_cat_load_section(&s37_cat_io, &s37_cat_map, &s37_cat_state,
                                (uint8_t)slot, p, &s37_cat_header);
}

static int s37_catalog_store_working(const project_t *p)
{
    if (!p || !proj_v7_valid(p) || s37_cat_uncertain)
        return -1;
    if (!s37_cat_ready)
        return s37_catalog_write_full();
    if (s37_cat_prepare_working_overlay(&s37_cat_overlay, &s37_cat_io, &s37_cat_map,
                                        &s37_cat_state, &s37_cat_scratch, (project_v7_t *)p) ||
        sm_save_stream(&s37_cat_io, &s37_cat_map, s37_cat_overlay_read,
                       &s37_cat_overlay, S37_CAT_PAYLOAD_SIZE, &s37_cat_state) ||
        s37_cat_validate(&s37_cat_io, &s37_cat_map, &s37_cat_state,
                         &s37_cat_scratch, &s37_cat_header))
        return -1;
    return 0;
}


#endif
static uint32_t autosave_hash, autosave_ms, autosave_checked;

UI_SMALL static int audio_quiet(void)
{
    uint32_t p, i;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++)
            if (trk[p].v[i].active)
                return 0;
    for (i = 0; i < NDRUM; i++)
        if (drums.v[i].active)
            return 0;
    return 1;
}

UI_SMALL static void autosave_tick(void)                /* main loop */
{
#if FELUCCA_FLASH
#if BOARD_SMK37
    if (project_backup_stage_active())
        return;
#endif
    uint32_t h, now = fm1_ms;
    if (!flash_ok || song.playing || transport_req || rec_wait || ft_on || ui.menu ||
        now - ui_input_ms < AUTOSAVE_IDLE || now - autosave_ms < AUTOSAVE_GAP || now - autosave_checked < 1000u)
        return;
    autosave_checked = now;
    proj_capture(&autosave_buf);
    h = autosave_buf.sum;
    if (!project_valid(&autosave_buf) || h == autosave_hash || !audio_quiet())
        return;
#if BOARD_SMK37
    if (s37_catalog_store_working(&autosave_buf) == 0)
        autosave_hash = h;
#else
    if (st_save(OBJ_AUTOSAVE, &autosave_buf, sizeof autosave_buf) == 0)
        autosave_hash = h;
#endif
    autosave_ms = fm1_ms;
#endif
}

static uint8_t drch_seed;                           /* legacy settings migration */
UI_SMALL static void autosave_resume(void)              /* power-on: restore the last working project */
{
#if FELUCCA_FLASH
    project_t *q = &autosave_buf;
    int n;
    if (!flash_ok)
        return;
#if BOARD_SMK37
    if (!s37_cat_ready) {
        if (s37_cat_uncertain)
            return;
        n = st_load(OBJ_AUTOSAVE, &proj_tmp, sizeof proj_tmp);
        project_v6_legacy_t *stage = (project_v6_legacy_t *)proj_slot[7];
        if (n <= 0 || !proj_import(stage, &proj_tmp, n) ||
            !proj_v6_to_v7(q, stage, sizeof *stage, 0, TP, ENGINES[ENGI_ANALOG]->edit))
            return;
        s37_migrate_pending = 1u;
    } else if (!project_valid(q)) {
        return;
    }
#else
    n = st_load(OBJ_AUTOSAVE, &proj_tmp, sizeof proj_tmp);
    if (!proj_import(q, &proj_tmp, n))
        return;
#endif
    autosave_hash = q->sum;
    if (drch_seed && !(q->rsv[0] & 0x80u) && q->g[G_DRCH] >= 0 && q->g[G_DRCH] <= 16 && q->g[G_DRCH] != midi_set[3]) {
        midi_set[3] = (uint8_t)q->g[G_DRCH];
        settings_later = 1;
    }
    drch_seed = 0;
    proj_apply(q, 1);
    song.sel = (uint8_t)(q->sel < NTRK ? q->sel : 0u);
    proj_foreign_parts(q);
    for (n = 0; n < NPART; n++)
        trk[n].engine = trk[n].eng_req;
#endif
}

/* settings + learned panel table: one flash object. The flash copy wins at
 * boot (the .noinit copies are garbage after a power-off). */
#if BOARD_SMK37
typedef struct {
    uint8_t count, loop, reserved[2];
    arr_entry_t entry[16];
} arr_config_v22_t;
typedef struct {
    uint32_t magic, palette, lowcut, zoom;
    panel_t panel;
    arr_config_v22_t arrangement;
    uint32_t lights;
} persist_v22_t;
_Static_assert(sizeof(arr_config_v22_t) == 36u && __builtin_offsetof(persist_v22_t, lights) == 84u &&
               sizeof(persist_v22_t) == 88u, "legacy PER3 settings layout");
#endif

typedef struct {
    uint32_t magic, palette, lowcut, zoom;
    panel_t panel;
#if FELUCCA_ARRANGER
    arr_config_t arrangement;
#endif
    uint32_t lights;
} persist_t;
_Static_assert(sizeof(side_rec_t) == 80u && sizeof(side_rec_t) <= ST_SIDE_MAX, "the side record: theme, favourites");
static side_rec_t side_out;
#define PERSIST_SIZE_V22 84u
#if FELUCCA_ARRANGER
#if BOARD_SMK37
_Static_assert(__builtin_offsetof(persist_t, arrangement) == 48u && sizeof(arr_config_t) == 68u &&
               __builtin_offsetof(persist_t, lights) == 116u && sizeof(persist_t) == 120u,
               "System 37 settings hold 32 arrangement positions");
#else
_Static_assert(__builtin_offsetof(persist_t, arrangement) == 48u && sizeof(arr_config_t) == 36u &&
               sizeof(persist_t) == PERSIST_SIZE_V22 + 4u,
               "the FM-1 Song page retains its original settings layout");
#endif
#endif
#if BOARD_SMK37
#define PERSIST_MAGIC 0x50455234u                  /* "PER4": eight sections, 32 arrangement positions */
#elif FELUCCA_ARRANGER
#define PERSIST_MAGIC 0x50455233u                  /* "PER3": the original four-scene song order */
#else
#define PERSIST_MAGIC 0x50455232u
#endif
#if FELUCCA_FLASH
static persist_t persist_saved;
static void dsu_boot(void);
#endif

#if BOARD_SMK37 && FELUCCA_FLASH
/* SMK-37: the wheels' calibration (HARDWARE CALIBRATION, smk_wheel.h) in an object of its own (storage.c OBJ_WCAL), so
 * the settings object stays as SLOOP's; none, older data or out of range: the wheels calibrate themselves */
static smk_wcal_t wcal_saved;
static void wcal_boot(void)
{
    smk_wcal_t c;
    if (st_load(OBJ_WCAL, &c, sizeof c) == (int)sizeof c && c.magic == SMK_WCAL_MAGIC)
        wcal = c;                                  /* (each wheel's part checked where it is used) */
    wcal_saved = wcal;
}
static void wcal_save(void)
{
    if (memcmp(&wcal, &wcal_saved, sizeof wcal) && st_save(OBJ_WCAL, &wcal, sizeof wcal) == 0)
        wcal_saved = wcal;
}
/* SMK-37: DAW mode (MODE, CHANNEL, LOCAL, CC MAP, KNOBS, the USER map: smk_daw.h daw_store_t) in an object of its own
 * (storage.c OBJ_DAW); none or older data: SYNTH, channel 1, LOCAL OFF, the DEFAULT map, ABSOLUTE */
static daw_store_t daw_saved __attribute__((section(".pool")));
static void daw_boot(void)
{
    int n = st_load(OBJ_DAW, &daw_saved, sizeof daw_saved);
    if (n > 0)
        daw_store_take((const uint8_t *)&daw_saved, (uint32_t)n);
    daw_store_fill(&daw_saved);
}
static void daw_save(void)
{
    daw_store_t d;
    daw_store_fill(&d);
    if (memcmp(&d, &daw_saved, sizeof d) && st_save(OBJ_DAW, &d, sizeof d) == 0)
        daw_saved = d;
}
/* SMK-37: the HOME menu's own settings (KEY VELOCITY, TRS MIDI OUT, BEND UP / DOWN, LIMITER, KNOB LAYOUT) in an object
 * of their own (storage.c OBJ_SMKSET), so SLOOP's settings word and MIDI part stay SLOOP's. Earlier SMK-37 builds kept
 * them in spare bits of those (the settings word's bits 28..31, the MIDI part's last three bytes): with no object yet,
 * they are read from there once (smkset_boot) and written here from the next save on */
#define SMKSET_MAGIC 0x54534B53u                   /* "SKST" */
typedef struct {
    uint32_t magic;
    uint8_t vel, trs, bup, bdn, lim, klay, pad, midi_ch4, midi_ch5; /* MIDI channels for synth tracks 4 and 5 */
} smkset_t;
static smkset_t smkset_saved;
static uint8_t smkset_old_ml[3], smkset_old_have;  /* the MIDI part's last three bytes as read (midi_part_read) */
static void smkset_fill(smkset_t *s)
{
    memset(s, 0, sizeof *s);
    s->magic = SMKSET_MAGIC;
    s->vel = (uint8_t)(vel_mode % VEL_N);
    s->trs = (uint8_t)(trs_out != 0u);
    s->bup = (uint8_t)bend_up_byte(bend_up);
    s->bdn = (uint8_t)bend_dn_byte(bend_dn, bend_up);
    s->lim = (uint8_t)(lim_classic != 0u);
    s->klay = (uint8_t)(knob_layout != 0u);
    s->pad = (uint8_t)(padled_dim % 3u);
    s->midi_ch4 = (uint8_t)(midi_set[10] <= 16u ? midi_set[10] : 4u);
    s->midi_ch5 = (uint8_t)(midi_set[11] <= 16u ? midi_set[11] : 5u);
}
static void smkset_boot(void)                      /* after the settings (persist_boot, dsu_boot) */
{
    smkset_t s;
    int n;
    memset(&s, 0, sizeof s);
    n = st_load(OBJ_SMKSET, &s, sizeof s);
    if ((n == (int)sizeof s || n == (int)sizeof(s) - 1) && s.magic == SMKSET_MAGIC) {
        vel_mode = (uint8_t)(s.vel < VEL_N ? s.vel : VEL_DYNAMIC);
        trs_out = (uint8_t)(s.trs != 0u);
        bend_up = (uint8_t)bend_up_of_byte(s.bup);
        bend_dn = (uint8_t)bend_dn_of_byte(s.bdn, bend_up);
        lim_classic = (uint8_t)(s.lim & 1u);
        knob_layout = (uint8_t)(s.klay & 1u);
        knob_layout_apply();
        padled_dim_set(s.pad < 3u ? s.pad : 0u);
        if (n == (int)sizeof s) {
            midi_set[10] = (uint8_t)(s.midi_ch4 <= 16u ? s.midi_ch4 : 4u);
            midi_set[11] = (uint8_t)(s.midi_ch5 <= 16u ? s.midi_ch5 : 5u);
            smkset_fill(&smkset_saved);
        } else {
            midi_set[10] = 4u;
            midi_set[11] = 5u;
            memset(&smkset_saved, 0, sizeof smkset_saved); /* write the extended record at the next settings save */
            settings_later = 1;
        }
        song.g[G_CH4] = midi_set[10];
        song.g[G_CH5] = midi_set[11];
        return;
    }
    /* none: an earlier SMK-37 build's settings, or none at all (the defaults stay) */
    if (persist_saved.magic == PERSIST_MAGIC) {
        uint32_t w = persist_saved.lights;
        vel_mode = (uint8_t)vel_of_word(w);
        trs_out = (uint8_t)!(w >> 31);
        usb_rate = 0, theme_on = 0, rec_quant_off = 0;
        cpu1_off = 1, cpu1_units = 0, knob_accel_off = 0;
    }
    if (smkset_old_have) {
        bend_up = (uint8_t)bend_up_of_byte(smkset_old_ml[0]);
        bend_dn = (uint8_t)bend_dn_of_byte(smkset_old_ml[1], bend_up);
        lim_classic = (uint8_t)(smkset_old_ml[2] & 1u);
        knob_layout = (uint8_t)((smkset_old_ml[2] >> 1) & 1u);
        knob_layout_apply();
        flt_reso = 0;
        fm1_irq_off();
        midi_set[6] = midi_set[7] = midi_set[8] = midi_set[9] = 0;
        song.g[G_FXCH] = song.g[G_DRMAP] = song.g[G_CLKOUT] = song.g[G_RMCH] = 0;
        fm1_irq_on();
    }
    song.g[G_CH4] = midi_set[10];
    song.g[G_CH5] = midi_set[11];
    memset(&smkset_saved, 0, sizeof smkset_saved); /* written at the next save */
}
static void smkset_save(void)
{
    smkset_t s;
    smkset_fill(&s);
    if (memcmp(&s, &smkset_saved, sizeof s) && st_save(OBJ_SMKSET, &s, sizeof s) == 0)
        smkset_saved = s;
}
#endif

UI_SMALL static void persist_boot(void)                    /* before settings_init / panel_init */
{
#if FELUCCA_ARRANGER
    arr_defaults(&arrangement);
#endif
#if BOARD_SMK37
    daw_reset_user();                              /* SMK-37: the USER map starts as the DEFAULT (flash: daw_boot) */
#endif
#if FELUCCA_FLASH
    persist_t p;
    uint32_t f = irq_save();
    flash_ok = FL_JEDEC_OK(FL_FAR(fl_jedec_ram)());      /* the expected 1 MiB part, else stay RAM-only */
    irq_restore(f);
    if (!flash_ok)
        return;
    fl_plain_window_init();                        /* flash above 0x93000 (SMK 0x9D000) reads as plaintext through XIP
                                                    * (user sample sets are played from there) */
    st_owner_check();                              /* SMK: never parse another ported firmware's store (storage.c) */
#if BOARD_SMK37
    st_stock_boot();                               /* SMK-37: the stock mapping's marker over our samples: released */
#endif
    {
        uint32_t k;
        for (k = 0; k < SMP_USER_SLOTS; k++)
            smp_user_scan(k);
    }
    {
#if BOARD_SMK37
        persist_v22_t oldp;
        memset(&p, 0, sizeof p);
        memset(&oldp, 0, sizeof oldp);
        {
            int n = st_load(OBJ_SETTINGS, &p, sizeof p);
            if (n == (int)sizeof p && p.magic == PERSIST_MAGIC) {
                settings.magic = SETTINGS_MAGIC;
                settings.palette = p.palette;
                settings.lowcut = p.lowcut;
                settings.zoom = p.zoom;
                if (p.panel.magic == PANEL_MAGIC)
                    panel = p.panel;
                lights_from_word(p.lights);
                if (arr_valid(&p.arrangement, 255u))
                    arrangement = p.arrangement;
                else
                    song_dirty = 1u;
                persist_saved = p;
                {
                    side_rec_t r;
                    if (st_side_load(OBJ_SETTINGS, &r, sizeof r) == 0)
                        side_take(&r);
                }
            } else if (n == (int)sizeof oldp || n == (int)PERSIST_SIZE_V22) {
                memcpy(&oldp, &p, (uint32_t)n < sizeof oldp ? (uint32_t)n : sizeof oldp);
                if ((n == (int)sizeof oldp && oldp.magic == 0x50455233u) ||
                    (n == (int)PERSIST_SIZE_V22 && oldp.magic == 0x50455232u)) {
                    p.magic = PERSIST_MAGIC;
                    p.palette = oldp.palette;
                    p.lowcut = oldp.lowcut;
                    p.zoom = oldp.zoom;
                    p.panel = oldp.panel;
                    arr_defaults(&p.arrangement);
                    if (n == (int)sizeof oldp) {
                        p.arrangement.count = oldp.arrangement.count > 16u ? 16u : oldp.arrangement.count;
                        p.arrangement.loop = oldp.arrangement.loop;
                        memcpy(p.arrangement.reserved, oldp.arrangement.reserved, sizeof p.arrangement.reserved);
                        memcpy(p.arrangement.entry, oldp.arrangement.entry, sizeof oldp.arrangement.entry);
                        p.lights = oldp.lights;
                    } else {
                        p.lights = 0u;
                    }
                    settings.magic = SETTINGS_MAGIC;
                    settings.palette = p.palette;
                    settings.lowcut = p.lowcut;
                    settings.zoom = p.zoom;
                    if (p.panel.magic == PANEL_MAGIC)
                        panel = p.panel;
                    lights_from_word(p.lights);
                    if (arr_valid(&p.arrangement, 15u))
                        arrangement = p.arrangement;
                    else
                        arr_defaults(&arrangement);
                    persist_saved = p;
                    song_dirty = 1u; /* write PER4 on the next quiet settings flush */
                    {
                        side_rec_t r;
                        if (st_side_load(OBJ_SETTINGS, &r, sizeof r) == 0)
                            side_take(&r);
                    }
                }
            } else if (n == (int)(8u + sizeof(panel_t)) && p.magic == 0x50455231u) {
                const uint32_t *w = (const uint32_t *)&p;
                panel_t old_panel;
                memcpy(&old_panel, w + 2, sizeof old_panel);
                settings.magic = SETTINGS_MAGIC;
                settings.palette = w[1];
                settings.lowcut = 0;
                settings.zoom = 0;
                if (old_panel.magic == PANEL_MAGIC)
                    panel = old_panel;
                song_dirty = 1u;
            }
        }
#else
        {
            int n = st_load(OBJ_SETTINGS, &p, sizeof p);
            if (n == (int)PERSIST_SIZE_V22 && p.magic == PERSIST_MAGIC)
                p.lights = 0;
            if (((n == (int)sizeof p || n == (int)PERSIST_SIZE_V22) && p.magic == PERSIST_MAGIC)
#if FELUCCA_ARRANGER
                || (n == (int)(16u + sizeof(panel_t)) && p.magic == 0x50455232u)
#endif
                ) {
                settings.magic = SETTINGS_MAGIC;
                settings.palette = p.palette;
                settings.lowcut = p.lowcut;
                settings.zoom = p.zoom;
                if (p.panel.magic == PANEL_MAGIC)
                    panel = p.panel;
                if (p.magic == PERSIST_MAGIC)
                    lights_from_word(p.lights);
                else
                    p.lights = 0;
#if FELUCCA_ARRANGER
                if (p.magic == PERSIST_MAGIC && arr_valid(&p.arrangement, 15u))
                    arrangement = p.arrangement;
                else
                    p.arrangement = arrangement;
#endif
                persist_saved = p;
                {
                    side_rec_t r;
                    if (st_side_load(OBJ_SETTINGS, &r, sizeof r) == 0)
                        side_take(&r);
                }
            } else if (n == (int)(8u + sizeof(panel_t)) && p.magic == 0x50455231u) {
                const uint32_t *w = (const uint32_t *)&p;
                panel_t old_panel;
                memcpy(&old_panel, w + 2, sizeof old_panel);
                settings.magic = SETTINGS_MAGIC;
                settings.palette = w[1];
                settings.lowcut = 0;
                settings.zoom = 0;
                if (old_panel.magic == PANEL_MAGIC)
                    panel = old_panel;
            }
        }
#endif
    }
#if BOARD_SMK37
    s37_catalog_boot();                            /* eight FUN7 sections plus the working autosave */
#if FELUCCA_ARRANGER
    if (!arr_valid(&arrangement, arrangement_ready())) {
        arr_defaults(&arrangement);
        song_dirty = 1u;
    }
#endif
#else
    {   /* FM-1: original four FUN6 project slots */
        uint32_t i;
        for (i = 0; i < 4u; i++)
            if (!proj_ok(proj_slot[i])) {
                proj_fetch(i);
            } else {
                int n = st_load(OBJ_PROJECT0 + i, &proj_tmp, sizeof proj_tmp);
                if (n != (int)sizeof *proj_slot[i] || memcmp(&proj_tmp.v6, proj_slot[i], sizeof *proj_slot[i]))
                    sec_dirty |= (uint8_t)(1u << i);
            }
    }
#endif
    dsu_boot();                                    /* SLOOP 2.5: the SYN kits (after the settings) */
#if BOARD_SMK37
    wcal_boot();                                   /* SMK-37: the wheels' calibration */
    daw_boot();                                    /* SMK-37: DAW mode */
    smkset_boot();                                 /* SMK-37: the HOME menu's own settings */
#endif
    up_boot();                                     /* user presets */
    fm6_bank_boot();                               /* the FM6 patch bank (fm6_bank.c) */
#endif
}

UI_SMALL static int project_used(uint32_t slot) { return project_valid(proj_slot[slot & (BOARD_SMK37 ? 7u : 3u)]); }

#if FELUCCA_FLASH
/* SLOOP 2.5: the settings object holds the SYN kits after persist_t (drum_synth.c dsu): firmware before 2.5 reads
 * its persist_t and leaves the rest (st_load cuts at its size); its own next save drops them. 2.6: then the MIDI LEARN
 * map (seq.c ml_tab, ML_N entries), only while something is learned; 2.5 reads an object with the map as one without
 * its SYN kits (their defaults), one without it as its own */
/* the settings object's third part (2.6): the MIDI LEARN map (seq.c ml_tab, ML_N entries, 2 bytes each, low first),
 * then GLO > MIDI: CH1 CH2 CH3, the drum channel | OTHER << 7, GLO > MIDI 2's BEND, 3 spare (earlier SMK-37 builds:
 * BEND UP, BEND DOWN, LIMITER | KNOB LAYOUT << 1; read until OBJ_SMKSET exists). Written only while it is not
 * the default (nothing learned, channels 1 2 3 10, OTHER SEL, BEND 2): else the object is exactly 2.5's. 2.6 test
 * builds wrote 32 (the map) or 36 bytes (and the channels) */
#define MIDI_PART (2u * ML_N + 8u)
_Static_assert(sizeof(persist_t) + sizeof(dsu_bank_t) + MIDI_PART <= ST_PAYLOAD_MAX, "the settings object fits its sector");
static uint8_t midi_saved[MIDI_PART];
static uint8_t drch_seed;                           /* a settings object as 2.5 wrote it: the drum channel was the
                                                     * project's then (autosave_resume takes it from a 2.5 autosave) */               /* the part as flash has it (settings_save: written when it differs) */
UI_SMALL static void midi_part_fill(uint8_t *b)
{
    uint32_t k;
    for (k = 0; k < ML_N; k++)
        b[2u * k] = (uint8_t)ml_tab[k], b[2u * k + 1u] = (uint8_t)(ml_tab[k] >> 8);
    b[2u * ML_N] = midi_set[0], b[2u * ML_N + 1u] = midi_set[1], b[2u * ML_N + 2u] = midi_set[2];
    b[2u * ML_N + 3u] = (uint8_t)(midi_set[3] | (drum_preview != 0u) << 5 | (step_adv_off != 0u) << 6 | midi_set[4] << 7);
                                                    /* (2.7.1: bit 5 DRUM PREVIEW, bit 6 STEP ADVANCE OFF: bits 2.7 does not read) */
    b[2u * ML_N + 4u] = (uint8_t)(midi_set[5] | (fxfade > 15u ? 15u : fxfade) << 4);   /* BEND (bits 0-3); 2.7.2: FX FADE bits 4-7 (0 = none: the byte reads as 2.6's BEND) */
    b[2u * ML_N + 5u] = midi_set[6];                /* (2.7) GLO > MIDI 3: FX CH (2.6 read this spare byte as nothing) */
    b[2u * ML_N + 6u] = (uint8_t)(midi_set[7] | midi_set[9] << 1 |
                                  (flt_reso > 10u ? 10u : flt_reso) << 2);   /* (2.7) GLO > MIDI 3: DR MAP, bit 1 CLK OUT
                                                                      * (a spare byte to 2.6); 2.7.2: FILTER RESO in bits
                                                                      * 2..5 (0 = none: the byte then reads as 2.7's) */
    b[2u * ML_N + 7u] = midi_set[8];                /* (2.7) GLO > MIDI 3: REMOTE (the last spare byte) */
}
UI_SMALL static int midi_part_default(const uint8_t *b)
{
    static const uint8_t def[12] = MIDI_SET_DEF;
    uint32_t k;
    for (k = 0; k < 2u * ML_N; k++)
        if (b[k])
            return 0;
    return b[2u * ML_N] == def[0] && b[2u * ML_N + 1u] == def[1] && b[2u * ML_N + 2u] == def[2] &&
           b[2u * ML_N + 3u] == (uint8_t)(def[3] | def[4] << 7) && b[2u * ML_N + 4u] == def[5] && b[2u * ML_N + 5u] == def[6] &&
           b[2u * ML_N + 6u] == (uint8_t)(def[7] | def[9] << 1) && b[2u * ML_N + 7u] == def[8];
}
/* n: 32 (the map), 36 (and the channels) or MIDI_PART; every entry checked (a CC that learns, a parameter this knows, a
 * channel 0..16, a range 1..12) */
UI_SMALL static void midi_part_read(const uint8_t *b, uint32_t n)
{
    uint32_t k, e;
    fm1_irq_off();                                  /* (the ISR reads the map and the channels: never half of them) */
    for (k = 0; k < ML_N; k++) {
        e = (uint32_t)b[2u * k] | (uint32_t)b[2u * k + 1u] << 8;
        ml_tab[k] = (uint16_t)((e >> 9) && ml_free_cc(e & 127u) && ml_id(e >> 9) < P_COUNT ? e : 0u);
    }
    if (n >= 2u * ML_N + 4u) {
        for (k = 0; k < 3u; k++)
            midi_set[k] = (uint8_t)(b[2u * ML_N + k] <= 16u ? b[2u * ML_N + k] : k + 1u);
        midi_set[3] = (uint8_t)((b[2u * ML_N + 3u] & 31u) <= 16u ? b[2u * ML_N + 3u] & 31u : 10u);
        midi_set[4] = (uint8_t)(b[2u * ML_N + 3u] >> 7);
        drum_preview = (uint8_t)((b[2u * ML_N + 3u] >> 5) & 1u);   /* (2.7.1) menu DRUM PREVIEW; older: OFF */
        step_adv_off = (uint8_t)((b[2u * ML_N + 3u] >> 6) & 1u);   /* (2.7.1) menu STEP ADVANCE; older: ON */
        song.g[G_CH1] = midi_set[0], song.g[G_CH2] = midi_set[1], song.g[G_CH3] = midi_set[2];
        song.g[G_DRCH] = midi_set[3], song.g[G_CHOTH] = midi_set[4];
    }
    if (n >= MIDI_PART) {
        midi_set[5] = (uint8_t)((b[2u * ML_N + 4u] & 15u) >= 1u && (b[2u * ML_N + 4u] & 15u) <= 12u ? b[2u * ML_N + 4u] & 15u : 2u);
        song.g[G_BEND] = midi_set[5];
        fxfade = (uint8_t)((b[2u * ML_N + 4u] >> 4) & 15u);         /* (2.7.2) FX FADE (bits 4-7); older: 0 */
        song.g[G_FXFADE] = fxfade;
        midi_set[6] = (uint8_t)(b[2u * ML_N + 5u] <= 16u ? b[2u * ML_N + 5u] : 0u);   /* (2.7) FX CH */
        song.g[G_FXCH] = midi_set[6];
        midi_set[7] = (uint8_t)(b[2u * ML_N + 6u] & 1u);             /* (2.7) DR MAP */
        midi_set[9] = (uint8_t)((b[2u * ML_N + 6u] >> 1) & 1u);      /* (2.7) CLK OUT */
        flt_reso = (uint8_t)((b[2u * ML_N + 6u] >> 2) & 15u);        /* (2.7.2) FILTER RESO (bits 2..5); older: 0 */
        if (flt_reso > 10u)
            flt_reso = 10u;
        song.g[G_DRMAP] = midi_set[7];
        song.g[G_CLKOUT] = midi_set[9];
        midi_set[8] = (uint8_t)(b[2u * ML_N + 7u] <= 16u ? b[2u * ML_N + 7u] : 0u);   /* (2.7) REMOTE */
        song.g[G_RMCH] = midi_set[8];
#if BOARD_SMK37
        /* earlier SMK-37 builds' BEND UP / DOWN, LIMITER | KNOB LAYOUT: for smkset_boot, while there is no OBJ_SMKSET */
        smkset_old_ml[0] = b[2u * ML_N + 5u], smkset_old_ml[1] = b[2u * ML_N + 6u], smkset_old_ml[2] = b[2u * ML_N + 7u];
        smkset_old_have = 1;
#endif
    }
    fm1_irq_on();
}
UI_SMALL static int settings_write(const persist_t *p)
{
    uint8_t mp[MIDI_PART];
    int rc;
    if (!dsu_valid(&dsu))
        dsu_defaults();
    midi_part_fill(mp);
    /* the third part only when it is not the default: without, the object is exactly 2.5's (2.5 reads it whole, SYN
     * kits included; with it, it takes the object for one without them). 2.7: the theme in the side record */
    side_fill(&side_out);
    st_side = &side_out;
    st_side_len = sizeof side_out;
    rc = st_save3(OBJ_SETTINGS, p, sizeof *p, &dsu, sizeof dsu, mp, midi_part_default(mp) ? 0u : sizeof mp);
    st_side = 0;
    if (rc)
        return -1;
    dsu_dirty = 0;
    fav_dirty = 0;
    memcpy(midi_saved, mp, sizeof midi_saved);
    return 0;
}
static void dsu_boot(void)                          /* the SYN kits (and the MIDI part) from the settings object, else */
{                                                   /* the defaults */
    st_hdr_t h;
    uint32_t len = sizeof(persist_t) + sizeof(dsu_bank_t), k;
    dsu_defaults();
    for (k = 0; k < ML_N; k++)
        ml_tab[k] = 0;
    if (st_current(OBJ_SETTINGS, &h) >= 0 &&
        (h.len == len || h.len == len + 2u * ML_N || h.len == len + 2u * ML_N + 4u || h.len == len + MIDI_PART) &&
        dsu_valid((const dsu_bank_t *)(st_buf + sizeof(persist_t)))) {
        memcpy(&dsu, st_buf + sizeof(persist_t), sizeof dsu);
        for (k = 0; k < DSU_N; k++)
            dsu_fix_kit(&dsu.k[k]);
        if (h.len > len)
            midi_part_read(st_buf + len, h.len - len);
        drch_seed = h.len == len;
    }
    midi_part_fill(midi_saved);
    dsu_dirty = 0;
}
#endif

UI_SMALL static void persist_fill(persist_t *p)              /* the settings as they are now */
{
    memset(p, 0, sizeof *p);
    p->magic = PERSIST_MAGIC;
    p->palette = settings.palette;
    p->lowcut = settings.lowcut;
    p->zoom = settings.zoom;
    p->panel = panel;
    p->lights = lights_word();
#if FELUCCA_ARRANGER
    p->arrangement = arrangement;
#endif
}

UI_SMALL static void settings_save(void)
{
#if FELUCCA_FLASH
    persist_t p;
    if (!flash_ok)
        return;
    persist_fill(&p);
#if BOARD_SMK37
    wcal_save();
    daw_save();
    smkset_save();
#endif
    {
        uint8_t mp[MIDI_PART];
        midi_part_fill(mp);
        if (!memcmp(&p, &persist_saved, sizeof p) && !memcmp(mp, midi_saved, sizeof mp) && !fav_dirty)
            return;                                /* unchanged: no erase cycle */
    }
    if (settings_write(&p) == 0)
        persist_saved = p;
#endif
}

#if FELUCCA_FLASH
#if BOARD_SMK37
_Static_assert(sizeof(project_v6_legacy_t) <= ST_PAYLOAD_MAX, "legacy FUN6 object fits its original sector");
#else
_Static_assert(sizeof(project_t) <= ST_PAYLOAD_MAX, "FM-1 FUN6 project fits one flash sector");
#endif

/* ---- backup restore (editor.c BK_PUT): each object checked as a load checks it, then written through the
 * same A/B commit as a save. rc: 0 ok, 2 not a valid object, 3 stop the song first, 4 flash */
UI_SMALL static int panel_valid(const panel_t *q)           /* a permutation of the buttons and of the knobs */
{
    uint32_t i, b = 0, e = 0;
    if (q->magic != PANEL_MAGIC)
        return 0;
    for (i = 0; i < NB; i++) {
        if (q->btn[i] >= 14u || (b >> q->btn[i]) & 1u)
            return 0;
        b |= 1u << q->btn[i];
    }
    for (i = 0; i < NE; i++) {
        if (q->enc[i] >= 7u || (e >> q->enc[i]) & 1u || (q->dir[i] != 1 && q->dir[i] != -1))
            return 0;
        e |= 1u << q->enc[i];
    }
    return 1;
}

UI_SMALL static uint32_t settings_restore(const void *raw, uint32_t n)
{
    persist_t p;
    if (n != sizeof p && n != PERSIST_SIZE_V22)
        return 2;
    memset(&p, 0, sizeof p);
    memcpy(&p, raw, n);
    if (p.magic != PERSIST_MAGIC || p.palette >= NPALETTES || p.lowcut > 1u || p.zoom > 1u || !panel_valid(&p.panel))
        return 2;
#if FELUCCA_ARRANGER
#if BOARD_SMK37
    if (!arr_valid(&p.arrangement, 255u))
#else
    if (!arr_valid(&p.arrangement, 15u))
#endif
        return 2;
    if ((song.playing || transport_req) && memcmp(&p.arrangement, &arrangement, sizeof arrangement))
        return 3;                                    /* SLOOP 2.5: a new song order only when stopped (as on the SONG screen) */
#endif
    if (!flash_ok || settings_write(&p))
        return 4;
    persist_saved = p;
    settings.palette = p.palette;
    settings.lowcut = p.lowcut;
    settings.zoom = p.zoom;
    panel = p.panel;
#if FELUCCA_ARRANGER
    arrangement = p.arrangement;
#endif
    lights_from_word(p.lights);
    song.g[G_SYNC] = (int16_t)lights_sync;
    song.g[G_MIDI] = (int16_t)lights_mout;
    song.g[G_ROUTE] = (int16_t)lights_min;
    palette_set(settings.palette);
    fx_lowcut = (uint8_t)(settings.lowcut != 0);
    ui.force = 1;
    return 0;
}

/* System 37: sections 0..7; slot 8 is the working project. FM-1 retains 0..3 / working slot 4. */
UI_SMALL static uint32_t project_restore(uint32_t slot, const void *raw, uint32_t n)
{
    if (song.playing || transport_req)
        return 3;
#if BOARD_SMK37
    if (slot < 8u && !n) {
        if (!flash_ok || s37_catalog_erase_section(slot))
            return 4;
        memset(proj_slot[slot], 0, sizeof *proj_slot[slot]);
        sec_dirty &= (uint8_t)~(1u << slot);
        return 0;
    }
    if (slot == 8u) {
        if (!proj_import_v7(&autosave_buf, raw, n))
            return 2;
        if (!flash_ok || s37_cat_uncertain)
            return 4;
        if (s37_cat_ready) {
            if (s37_catalog_store_working(&autosave_buf))
                return 4;
            project_apply(&autosave_buf);
        } else {
            project_apply(&autosave_buf);
            if (s37_catalog_write_full())
                return 4;
        }
        autosave_hash = autosave_buf.sum;
        return 0;
    }
    if (slot >= 8u || !proj_import_v7(&autosave_buf, raw, n))
        return 2;
    if (!flash_ok)
        return 4;
    if (s37_cat_ready) {
        if (s37_catalog_store_section(slot, &autosave_buf))
            return 4;
        memcpy(proj_slot[slot], &autosave_buf, sizeof *proj_slot[slot]);
        sec_dirty &= (uint8_t)~(1u << slot);
    } else {
        memcpy(proj_slot[slot], &autosave_buf, sizeof *proj_slot[slot]);
        sec_dirty |= (uint8_t)(1u << slot);
        if (s37_catalog_write_full())
            return 4;
        sec_dirty = 0u;
    }
    return 0;
#else
    if (slot < 4u && !n) {
        if (!flash_ok || st_save(OBJ_PROJECT0 + slot, raw, 0))
            return 4;
        memset(proj_slot[slot], 0, sizeof *proj_slot[slot]);
        sec_dirty &= (uint8_t)~(1u << slot);
        return 0;
    }
    if (!proj_import(&autosave_buf, raw, (int)n))
        return 2;
    if (slot == 4u) {
        project_apply(&autosave_buf);
        return 0;
    }
    if (slot >= 4u || !flash_ok || st_save(OBJ_PROJECT0 + slot, &autosave_buf, sizeof autosave_buf))
        return 4;
    memcpy(proj_slot[slot], &autosave_buf, sizeof *proj_slot[slot]);
    sec_dirty &= (uint8_t)~(1u << slot);
    return 0;
#endif
}
#endif
#if FELUCCA_ARRANGER
UI_SMALL static void arrangement_save(void)
{
    if (song.playing || transport_req) { ui_message("STOP BEFORE SAVE"); return; }
    settings_save();
#if FELUCCA_FLASH
    if (flash_ok) {
        ui_message(memcmp(&persist_saved.arrangement, &arrangement, sizeof arrangement) ? "SAVE ERROR" : "SONG SAVED");
        return;
    }
#endif
    ui_message("SONG IN RAM ONLY");
}

/* ---- live sections (SAVE + key, ui_layers.c). A section is a project slot (A..D = 1..4): stored into RAM
 * at once (playing too), written to flash once the transport is stopped and nothing sounds (an erase
 * stops the audio for ~50 ms); a song recorded with SONG REC is saved the same way. */
UI_SMALL static void section_store(uint32_t s)
{
    s &= (BOARD_SMK37 ? 7u : 3u);
    fm1_irq_off();                                      /* (the audio ISR may be applying a section) */
    proj_capture(proj_slot[s]);
    live_sec = (int8_t)s;
    fm1_irq_on();
    sec_dirty |= (uint8_t)(1u << s);
}
UI_SMALL static void section_load(uint32_t s)                    /* stopped: the section is the loop now */
{
    s &= (BOARD_SMK37 ? 7u : 3u);
    project_apply(proj_slot[s]);
    live_sec = (int8_t)s;
}
UI_SMALL static void sections_write(void)                        /* the dirty sections and song into flash */
{
#if FELUCCA_FLASH
#if BOARD_SMK37
    if (flash_ok && (sec_dirty || s37_migrate_pending) && !s37_cat_uncertain) {
        if (s37_catalog_write_full() == 0) {
            sec_dirty = 0u;
            autosave_hash = autosave_buf.sum;
        }
    }
#else
    uint32_t i;
    if (flash_ok)
        for (i = 0; i < 4u; i++)
            if (((sec_dirty >> i) & 1u) && st_save(OBJ_PROJECT0 + i, proj_slot[i], sizeof *proj_slot[i]) == 0)
                sec_dirty &= (uint8_t)~(1u << i);
    if (!flash_ok)
        sec_dirty = 0u;
#endif
#endif
    if (song_dirty || fav_dirty) {
        song_dirty = 0;
        settings_save();
    }
}

/* before an intentional reset (an update, UPDATE MODE, UBOOT from the host): the audio is stopped, so
 * whatever is only in RAM goes to flash now: the live sections, the song, the working project */
UI_SMALL static void persist_flush_now(void)
{
    sections_write();
#if FELUCCA_FLASH
    if (flash_ok && !arrangement_clock.running) {
        proj_capture(&autosave_buf);
#if BOARD_SMK37
        if (project_valid(&autosave_buf) && autosave_buf.sum != autosave_hash &&
            s37_catalog_store_working(&autosave_buf) == 0)
            autosave_hash = autosave_buf.sum;
#else
        if (autosave_buf.sum != autosave_hash && st_save(OBJ_AUTOSAVE, &autosave_buf, sizeof autosave_buf) == 0)
            autosave_hash = autosave_buf.sum;
#endif
    }
#endif
}
UI_SMALL static void sections_flush(void)                        /* main loop */
{
    static uint32_t tried;
    if (srec_done) {
        song_dirty = srec_done != 0xFFu;
        if (song_dirty) {
            char b[8];
            fmt_int(b, srec_done);
            ui_say("SONG PARTS ", b);
        } else {
            ui_message("NO SONG");
        }
        srec_done = 0;
    }
    if ((uint32_t)song.g[G_SYNC] != lights_sync) {      /* GLO > SYSTEM > SYNC: kept with the settings */
        lights_sync = (uint8_t)song.g[G_SYNC];
        settings_later = 1;
    }
    if ((uint32_t)(song.g[G_MIDI] != 0) != lights_mout) {   /* GLO > SYSTEM > MIDI: the same */
        lights_mout = (uint8_t)(song.g[G_MIDI] != 0);
        settings_later = 1;
    }
    if ((uint32_t)song.g[G_ROUTE] != lights_min) {        /* GLO > SYSTEM > IN: the same (2.6: CH n too) */
        lights_min = (uint8_t)song.g[G_ROUTE];
        settings_later = 1;
    }
    {   /* 2.6: GLO > MIDI (CH1 CH2 CH3 OTHER) and GLO > DRUMS > CH: the same */
        static const uint8_t gid[12] = {G_CH1, G_CH2, G_CH3, G_DRCH, G_CHOTH, G_BEND, G_FXCH, G_DRMAP, G_RMCH, G_CLKOUT, G_CH4, G_CH5};
        uint32_t k;
        for (k = 0; k < 12u; k++)
            if ((uint32_t)song.g[gid[k]] != midi_set[k]) {
                midi_set[k] = (uint8_t)song.g[gid[k]];
                settings_later = 1;
            }
        if ((uint32_t)song.g[G_FXFADE] != fxfade) {   /* (2.7.2) GLO > MASTER 2 > FADE: a setting of the FM-1 */
            fxfade = (uint8_t)song.g[G_FXFADE];
            settings_later = 1;
        }
    }
    if (settings_later) {                               /* the menu closed while playing */
        settings_later = 0;
        song_dirty = 1;                                 /* (settings_save when quiet, with the song) */
    }
    if ((!sec_dirty && !song_dirty
#if BOARD_SMK37
         && !s37_migrate_pending
#endif
        ) || song.playing || transport_req || !audio_quiet() || fm1_ms - ui_input_ms < 1500u ||
        fm1_ms - tried < 5000u)
        return;
    tried = fm1_ms;                                     /* (a failed write: again in 5 s, not every frame) */
    sections_write();
    if (sec_dirty)
        ui_message("SAVE ERROR: RETRYING");
}
#endif
#endif /* PROJ_HOST */
