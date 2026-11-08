/* SPDX-License-Identifier: GPL-3.0-only */
/* System37 SMK-37 Pro fader pages: A1/A2 levels, B filter, C reverb, D delay, E envelope.
 * Renders the six-page layout, including the second level bank for tracks 5 and 6. */
#define UI_PAGES_HARNESS_ONLY 1
#include "ui_pages_test.c"
static uint8_t smk_fbank, smk_fbank_new;
static char knob_digit(uint32_t role) { return role == EN_SELECT ? '2' : '5'; }
#include "../firmware/src/ui_fbank.c"

static void fb_shot(const char *name)
{
    ui.frame++;
    fb_draw();
    ppm(name);
}

int main(int argc, char **argv)
{
    uint32_t i;
    int16_t *vp;
    outdir = argc > 1 ? argv[1] : "build/host";
    panel = PANEL_DEFAULT;
    layers_init();
    settings.palette = 4;
    palette_set(4);
    host_tracks_init();
    for (i = 0; i < NPART; i++) {
        set_engine_of(&trk[i], TRK_DEF[i][0]);
        apply_preset_to(&trk[i], TRK_DEF[i][1]);
        trk[i].engine = trk[i].eng_req;
    }
    go_home(); ui.force = 1; frame();

    check(FB_N == 6u, "System37 has six fader pages");
    fb_set(smk_fbank + 1u);
    check(fb_shown() && smk_fbank == 1u && smk_fbank_new, "A1 -> A2: the second level page opens");
    fb_set(smk_fbank + FB_N - 1u);
    check(smk_fbank == 0u, "SELECT back: A1");
    fb_set(4u);
    fb_set(5u);
    check(smk_fbank == 5u, "D -> E: envelope page follows delay");
    fb_set(5u + 1u);
    check(smk_fbank == 0u, "E -> A1 wraps");
    fb_set(FB_N - 1u);
    check(smk_fbank == 5u, "A1 backward -> E");

    song.sel = 1u;
    check(fb_param(5u, 0u, &vp) == &TP[P_ATK] && vp == &trk[1].p[P_ATK] &&
          fb_param(5u, 3u, &vp) == &TP[P_REL] && vp == &trk[1].p[P_REL],
          "bank E: selected synth's ATTACK .. RELEASE");
    ui.force = 1; fb_shot("fbank-e");
    song.sel = TRK_DRUM;
    check(!fb_param(5u, 0u, &vp) && !fb_param(5u, 3u, &vp),
          "bank E: drum track has no synth envelope faders");
    ui.force = 1; fb_shot("fbank-e-drums");

    song.sel = 0u;
    check(fb_param(0u, 0u, &vp) == &TP[P_LEVEL] && vp == &trk[0].p[P_LEVEL] &&
          fb_param(0u, 3u, &vp) == &TP[P_LEVEL] && vp == &trk[3].p[P_LEVEL] &&
          !fb_param(0u, 4u, &vp), "bank A1: faders 1-4 map to synth tracks 1-4 only");
    check(fb_param(1u, 0u, &vp) == &TP[P_LEVEL] && vp == &trk[4].p[P_LEVEL] &&
          fb_param(1u, 1u, &vp) == &GP[G_DRLVL] && vp == &song.g[G_DRLVL] &&
          !fb_param(1u, 2u, &vp), "bank A2: fader 1 = track 5, fader 2 = drums, 3-4 unused");
    check(fb_param(2u, 3u, &vp) == &TP[P_TFLT] && vp == &trk[3].p[P_TFLT] &&
          !fb_param(2u, 4u, &vp), "bank B: filter faders cover synth tracks 1-4");
    check(fb_param(3u, 0u, &vp) == &TP[P_REV] && vp == &trk[0].p[P_REV] &&
          fb_param(4u, 0u, &vp) == &TP[P_DLY] && vp == &trk[0].p[P_DLY],
          "banks C/D: reverb and delay map to synth tracks");

    fb_set(0u); ui.force = 1; fb_shot("fbank-a1");
    fb_set(1u); ui.force = 1; fb_shot("fbank-a2");
    fb_set(2u); ui.force = 1; fb_shot("fbank-b");

    ui.layer = LY_STEP;
    check(!fb_shown(), "hidden while a layer is held");
    ui.layer = LY_PLAY;
    fm1_ms += 60000u;
    check(fb_shown(), "stays up until FADER BANK / PATCH");
    fb_set(4u); fb_set(smk_fbank + 1u);
    check(smk_fbank == 5u, "K2 advances D -> E");
    fb_close();
    check(!fb_shown(), "closed");
    printf("fbank: %s\n", fails ? "FAIL" : "ok");
    return fails != 0;
}
