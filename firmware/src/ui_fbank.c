/* SPDX-License-Identifier: GPL-3.0-only
 * SMK-37 Pro: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* System 37 keeps the four physical faders and adds a second LEVEL page for tracks 5 and 6.
 * The cycle is A1 (tracks 1-4), A2 (tracks 5-6), B FILTER, C REVERB, D DELAY, E ENV. */
#ifndef FB_N
#define FB_N 6u
#endif
static const char *const FB_NAME[FB_N] = {"LVL 1-4", "LVL 5-6", "FILTR", "REVRB", "DELAY", "ENV"};
static const char *const FB_LETTER[FB_N] = {"A1", "A2", "B", "C", "D", "E"};
static const char *const FB_ENV[4] = {"ATTACK", "DECAY", "SUSTAIN", "RELEASE"};
#define FB_SHOW_MS 4000u                           /* up this long after the last change */
static uint8_t fb_open;
static uint32_t fb_t, fb_frame;                    /* last action (ms); the frame drawn last */

static int fb_shown(void)
{
    return fb_open && !ui.menu && ui.layer == LY_PLAY && !ui.hold_kind;
}
static void fb_set(uint32_t b)                     /* bank b, the screen up */
{
    smk_fbank = (uint8_t)(b % FB_N);
    smk_fbank_new = 1;
    fb_open = 1;
    fb_t = fm1_ms;
    ui.force = 1;
}
static void fb_close(void) { fb_open = 0; }
static void fb_expire(void)                        /* up FB_SHOW_MS after the last change */
{
    if (fb_open && fm1_ms - fb_t > FB_SHOW_MS)
        fb_open = 0, ui.force = 1;
}
static int fb_left(void)                           /* drawn last frame, not now: the page needs a redraw */
{
    int l = fb_frame && fb_frame + 1u == ui.frame;
    if (l)
        fb_frame = 0;
    return l;
}

/* Resolve a fader row to its track. A2 maps faders 1 and 2 to tracks 5 and 6. */
static int fb_track(uint32_t b, uint32_t i, uint32_t *track)
{
    if (b == 1u) {
        if (i >= 2u) return 0;
        *track = 4u + i;
        return 1;
    }
    if (b == 0u || (b >= 2u && b <= 4u)) {
        *track = i;
        return 1;
    }
    return 0;
}

/* What fader i sets in bank b. Bank E edits the selected synth's envelope. */
static const param_desc_t *fb_param(uint32_t b, uint32_t i, int16_t **vp)
{
    uint32_t track;
    if (b == 5u) {
        if (is_drum(TSEL)) return 0;
        *vp = &TSEL->p[P_ATK + (i & 3u)];
        return &TP[P_ATK + (i & 3u)];
    }
    if (!fb_track(b, i, &track)) return 0;
    if (b <= 1u) {
        if (track == TRK_DRUM) {
            *vp = &song.g[G_DRLVL];
            return &GP[G_DRLVL];
        }
        *vp = &trk[track].p[P_LEVEL];
        return &TP[P_LEVEL];
    }
    if (b == 2u) {
        *vp = &trk[track].p[P_TFLT];
        return &TP[P_TFLT];
    }
    if (b == 3u) {
        *vp = track == TRK_DRUM ? &song.g[G_DRREV] : &trk[track].p[P_REV];
        return track == TRK_DRUM ? &GP[G_DRREV] : &TP[P_REV];
    }
    *vp = track == TRK_DRUM ? &song.g[G_DRDLY] : &trk[track].p[P_DLY];
    return track == TRK_DRUM ? &GP[G_DRDLY] : &TP[P_DLY];
}

/* (#7) a fader's position v (0..127) -> a synth track's LEVEL. LEVEL is in 1/2 dB steps (params.c F_DB: 0 off, 1 -55.5 dB ..
 * 112 0 dB .. 127 +7.5 dB); read as LEVEL steps, the travel's lower half was -55..-24 dB, heard as muted. GM's volume law
 * instead (40 log10(v / 127), as CC 11 EXPRESSION, core.h expr_gain): the gain (LEVEL_Q12) the nearest above
 * LEVEL_Q12[127] (v / 127)^2: 0 off, 1 the quietest step, half the travel -4 dB (LEVEL's power-on 104), the top 127 */
static int16_t fb_level(uint32_t v)
{
    uint32_t want = (LEVEL_Q12[127] * (v > 127u ? 127u : v) * (v > 127u ? 127u : v) + 16128u) / 16129u, l = 0;
    while (l < 127u && LEVEL_Q12[l] < want)
        l++;
    return (int16_t)l;
}

/* fader i moved to v (0..127) in bank b */
static void fb_move(uint32_t b, uint32_t i, uint32_t v)
{
    int16_t *pv;
    uint32_t track;
    if (b == 5u) {
        if (!is_drum(TSEL)) {
            const param_desc_t *d = &TP[P_ATK + (i & 3u)];
            TSEL->p[P_ATK + (i & 3u)] = (int16_t)(d->min + ((int32_t)d->max - d->min) * (int32_t)v / 127);
        }
        return;
    }
    if (!fb_track(b, i, &track)) return;
    if (b <= 1u) {
        if (track == TRK_DRUM) song.g[G_DRLVL] = (int16_t)v;
        else trk[track].p[P_LEVEL] = fb_level(v);
    } else if (b == 2u) {
        trk[track].p[P_TFLT] = (int16_t)((int32_t)v - 64);
    } else if (b == 3u) {
        pv = track == TRK_DRUM ? &song.g[G_DRREV] : &trk[track].p[P_REV];
        *pv = (int16_t)v;
    } else {
        pv = track == TRK_DRUM ? &song.g[G_DRDLY] : &trk[track].p[P_DLY];
        *pv = (int16_t)v;
    }
}

static void fb_draw(void)
{
    static uint32_t shown;
    uint32_t b = smk_fbank % FB_N, i, half, sig = b * 7919u + song.sel * 104729u;
    int16_t *vp;
    if (fb_frame + 1u != ui.frame) {               /* another screen was up: all of it */
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.force = 1;
    }
    fb_frame = ui.frame;
    for (i = 0; i < 4u; i++)
        if (fb_param(b, i, &vp))
            sig = sig * 31u + (uint32_t)(*vp + 200);
    if (!ui.force && sig == shown)
        return;
    shown = sig;
    for (half = 0; half < 2u; half++) {
        cv_begin(240, 120, C_BLACK);
        if (!half) {
            cv_text(4, 2, &FONT_L, "FADER BANK", C_WHITE);
            for (i = 0; i < FB_N; i++) {           /* six compact bank tiles */
                int32_t x = (int32_t)i * 40 + 1, on = i == b;
                cv_rect(x, 42, 38, 74, on ? C_WHITE : TE_G2);
                if (!on) cv_rect(x + 1, 43, 36, 72, TE_G1);
                cv_text(x + 19 - text_w(&FONT_L, FB_LETTER[i]) / 2, 48, &FONT_L, FB_LETTER[i], on ? C_BLACK : TE_G3);
                cv_text(x + 19 - text_w(&FONT_S, FB_NAME[i]) / 2, 90, &FONT_S, FB_NAME[i], on ? C_BLACK : TE_G3);
            }
        } else {
            for (i = 0; i < 4u; i++) {             /* four physical faders; A2 assigns only F1/F2 */
                uint32_t track = i, colour;
                int32_t y = 4 + (int32_t)i * 24, w;
                char lab[10] = "TRACK 1", val[12], v2[20];
                const char *unit = "";
                const param_desc_t *d = fb_param(b, i, &vp);
                char f[3] = {'F', (char)('1' + i), 0};
                if (!d) {
                    if (b == 1u && i == 2u) cv_text(4, 80, &FONT_S, "F1=TRACK 5   F2=TRACK 6", TE_G3);
                    else if (b == 5u && !i) cv_text(4, 28, &FONT_S, "ENV: SELECT A SYNTH TRACK", C_WHITE);
                    continue;
                }
                if (b == 5u) {
                    str_cpy(lab, FB_ENV[i], sizeof lab);
                    colour = song.sel % NTRK;
                } else {
                    (void)fb_track(b, i, &track);
                    if (track == TRK_DRUM) str_cpy(lab, "DRUMS", sizeof lab);
                    else { str_cpy(lab, "TRACK 1", sizeof lab); lab[6] = (char)('1' + track); }
                    colour = track;
                }
                param_format(d, *vp, val, &unit);
                str_cpy(v2, val, sizeof v2);
                str_cpy(v2 + str_len(v2), unit, sizeof v2 - str_len(v2));
                cv_text(4, y, &FONT_S, f, TE_COL[colour]);
                cv_text(32, y, &FONT_S, lab, TE_COL[colour]);
                cv_text(236 - text_w(&FONT_S, v2), y, &FONT_S, v2, C_WHITE);
                w = RATIO(d, *vp) * 204 / 1000;
                cv_rect(32, y + 17, 204, 3, TE_G1);
                cv_rect(32, y + 17, w < 1 ? 1 : w, 3, TE_COL[colour]);
            }
            {
                char h[24] = "< >  K2  FADER BANK";
                h[6] = knob_digit(EN_SELECT);          /* (SELECT: K2, or K6, HOME menu > SCREEN > KNOB LAYOUT) */
                cv_text((240 - text_w(&FONT_S, h)) / 2, 100, &FONT_S, h, TE_G3);
            }
        }
        cv_blit(0, half * 120u);
    }
}
