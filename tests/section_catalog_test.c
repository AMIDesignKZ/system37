/* SPDX-License-Identifier: GPL-3.0-only
 * Host tests for the System 37 A-H section catalogue.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "section_catalog.h"
#include "section_prefetch_io.h"

#define NOR_SIZE 0x40000u
static uint8_t nor[NOR_SIZE];

static int nor_read(void *ctx, uint32_t off, void *dst, uint32_t n)
{
    (void)ctx;
    if (off > NOR_SIZE || n > NOR_SIZE - off) return -1;
    memcpy(dst, nor + off, n);
    return 0;
}
static int nor_erase(void *ctx, uint32_t off)
{
    (void)ctx;
    if ((off & (SM_SECTOR_SIZE - 1u)) || off > NOR_SIZE - SM_SECTOR_SIZE) return -1;
    memset(nor + off, 0xFF, SM_SECTOR_SIZE);
    return 0;
}
static int nor_program(void *ctx, uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *p = (const uint8_t *)src;
    uint32_t i;
    (void)ctx;
    if (!n || n > SM_PAGE_SIZE || (off & (SM_PAGE_SIZE - 1u)) + n > SM_PAGE_SIZE ||
        off > NOR_SIZE || n > NOR_SIZE - off) return -1;
    for (i = 0; i < n; ++i) {
        if ((nor[off + i] & p[i]) != p[i]) return -2;
        nor[off + i] &= p[i];
    }
    return 0;
}
static int check(const char *label, int ok)
{
    printf("%-64s %s\n", label, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}
static void make_project(project_v7_t *p, uint32_t seed)
{
    uint32_t t, s;
    memset(p, 0, sizeof *p);
    p->magic = PROJ_V7_MAGIC;
    p->size = sizeof *p;
    p->sel = (uint8_t)(seed % 6u);
    p->drdly = (uint8_t)(seed + 10u);
    for (s = 0; s < 32u; ++s) p->g[s] = (int16_t)(seed * 10u + s);
    for (t = 0; t < PROJ_V7_TRACKS; ++t) {
        p->t[t].engine = (uint8_t)((seed + t) % NENGINES);
        p->t[t].preset = (uint8_t)(seed + t + 1u);
        for (s = 0; s < NSTEP; ++s) {
            p->t[t].step[s].time = ST_REST;
            p->t[t].step[s].note[0] = (uint8_t)(seed + t + s);
            p->t[t].fill[s / 4u] |= (uint8_t)(((s + seed) & 3u) << (2u * (s % 4u)));
        }
        for (s = 0; s < PROJ_V7_LOCKS; ++s)
            p->t[t].lock[s].step = LOCK_FREE;
    }
    p->sum = proj_v7_hash(p, (uint32_t)sizeof *p - 4u);
}

int main(void)
{
    static const sm_map_t map = {
        .object_id = 0x2001u,
        .max_length = S37_CAT_PAYLOAD_SIZE,
        .sectors = {
            {0x10000u,0x11000u,0x12000u,0x13000u,0x14000u,0x15000u,
             0x16000u,0x17000u,0x18000u,0x19000u,0x1A000u,0x1B000u},
            {0x20000u,0x21000u,0x22000u,0x23000u,0x24000u,0x25000u,
             0x26000u,0x27000u,0x28000u,0x29000u,0x2A000u,0x2B000u}
        },
        .sector_count = {12u,12u}
    };
    static project_v7_t scratch, replacement, loaded, working, expected[8];
    sm_io_t io = {0, nor_read, nor_erase, nor_program};
    sm_state_t state;
    s37_cat_header_t header = {0};
    s37_cat_overlay_t source;
    uint32_t i;
    int bad = 0;

    memset(nor, 0xFF, sizeof nor);
    bad += check("catalogue is 84-byte directory plus eight sections and one working record",
                 sizeof(s37_cat_header_t) == 84u && S37_CAT_PAYLOAD_SIZE == 48378u);
    bad += check("twelve-sector A/B extents provide sufficient capacity",
                 sm_map_valid(&map) && sm_capacity(&map, 0u) == 48896u &&
                 sm_capacity(&map, 1u) == 48896u &&
                 sm_capacity(&map, 0u) - S37_CAT_PAYLOAD_SIZE == 518u);
    {
        uint8_t blank[16];
        uint32_t j;
        int all_ff = 1;
        memset(blank, 0, sizeof blank);
        if (s37_cat_blank_read(NULL, 0x1234u, blank, sizeof blank) != 0)
            all_ff = 0;
        for (j = 0; j < sizeof blank; ++j)
            if (blank[j] != 0xFFu) all_ff = 0;
        bad += check("blank catalogue source returns erased-flash bytes", all_ff);
        bad += check("blank catalogue source rejects a null destination",
                     s37_cat_blank_read(NULL, 0u, NULL, sizeof blank) != 0);
    }

    make_project(&replacement, 1u);
    expected[0] = replacement;
    bad += check("prepare first catalogue with section A",
                 s37_cat_prepare_create(&source, &replacement, 0u, 4u, 0u) == 0);
    bad += check("first catalogue commit from streaming blank source",
                 sm_save_stream(&io, &map, s37_cat_create_read, &source,
                                S37_CAT_PAYLOAD_SIZE, &state) == 0);
    bad += check("first catalogue validates and section A loads",
                 sm_scan(&io, &map, &state) == 0 &&
                 s37_cat_validate(&io, &map, &state, &scratch, &header) == 0 &&
                 header.present_mask == 0x01u && header.default_section == 0u &&
                 header.section[0].bars == 4u &&
                 s37_cat_load_section(&io, &map, &state, 0u, &loaded, NULL) == 0 &&
                 !memcmp(&loaded, &expected[0], sizeof loaded));
    bad += check("absent section cannot be loaded",
                 s37_cat_load_section(&io, &map, &state, 1u, &loaded, NULL) != 0);
    {
        s37_prefetch_t pf;
        s37_pf_init(&pf);
        bad += check("prefetch loader fails closed for an absent catalogue section",
                     s37_pf_request(&pf, 1u) != 0u &&
                     s37_pf_service(&pf, &io, &map, &scratch) == -1 &&
                     pf.state == S37_PF_FAILED && !s37_pf_take(&pf, 1u));
        bad += check("prefetch loader publishes a validated section for one-shot consumption",
                     s37_pf_request(&pf, 0u) != 0u &&
                     s37_pf_service(&pf, &io, &map, &scratch) == 1 &&
                     s37_pf_take(&pf, 0u) && !memcmp(&scratch, &expected[0], sizeof scratch) &&
                     pf.state == S37_PF_IDLE);
    }
    make_project(&working, 55u);
    bad += check("prepare a working-project autosave overlay",
                 s37_cat_prepare_working_overlay(&source, &io, &map, &state, &scratch, &working) == 0);
    bad += check("commit working-project autosave without changing sections",
                 sm_save_stream(&io, &map, s37_cat_overlay_read, &source, S37_CAT_PAYLOAD_SIZE, &state) == 0);
    bad += check("working project validates and round-trips",
                 s37_cat_validate(&io, &map, &state, &scratch, &header) == 0 &&
                 s37_cat_load_working(&io, &map, &state, &loaded) == 0 &&
                 !memcmp(&loaded, &working, sizeof loaded));
    {
        s37_cat_header_t invalid = header;
        invalid.present_mask ^= 0x02u;
        bad += check("directory present-mask mismatch rejected", !s37_cat_header_valid(&invalid));
        invalid = header;
        invalid.reserved[0] = 1u;
        bad += check("unknown directory reserved bits rejected", !s37_cat_header_valid(&invalid));
        invalid = header;
        invalid.section[0].reserved = 1u;
        bad += check("unknown section-entry reserved bits rejected", !s37_cat_header_valid(&invalid));
    }

    for (i = 1; i < S37_CAT_SECTIONS; ++i) {
        make_project(&replacement, i + 1u);
        expected[i] = replacement;
        if (s37_cat_prepare_overlay(&source, &io, &map, &state, &scratch,
                                    &replacement, (uint8_t)i, (uint16_t)(i + 4u), 0u) ||
            sm_save_stream(&io, &map, s37_cat_overlay_read, &source,
                           S37_CAT_PAYLOAD_SIZE, &state)) {
            bad += check("streaming section overlay commits A-H catalogue", 0);
            return 1;
        }
    }
    bad += check("all eight sections present with per-section bar metadata",
                 s37_cat_validate(&io, &map, &state, &scratch, &header) == 0 &&
                 header.present_mask == 0xFFu && header.default_section == 0u &&
                 header.section[7].bars == 11u);
    {
        int all_match = 1;
        for (i = 0; i < S37_CAT_SECTIONS; ++i)
            all_match &= s37_cat_load_section(&io, &map, &state, (uint8_t)i, &loaded, NULL) == 0 &&
                         !memcmp(&loaded, &expected[i], sizeof loaded);
        bad += check("all eight project records round-trip independently", all_match);
    }

    make_project(&replacement, 99u);
    expected[3] = replacement;
    bad += check("prepare update of section D without changing other sections",
                 s37_cat_prepare_overlay(&source, &io, &map, &state, &scratch,
                                         &replacement, 3u, 16u, 7u) == 0);
    bad += check("commit section D overlay through segmented A/B storage",
                 sm_save_stream(&io, &map, s37_cat_overlay_read, &source,
                                S37_CAT_PAYLOAD_SIZE, &state) == 0);
    bad += check("updated directory and project checksums validate",
                 s37_cat_validate(&io, &map, &state, &scratch, &header) == 0 &&
                 header.present_mask == 0xFFu && header.default_section == 7u &&
                 header.section[3].bars == 16u);
    {
        int all_match = 1;
        for (i = 0; i < S37_CAT_SECTIONS; ++i)
            all_match &= s37_cat_load_section(&io, &map, &state, (uint8_t)i, &loaded, NULL) == 0 &&
                         !memcmp(&loaded, &expected[i], sizeof loaded);
        bad += check("section D replacement leaves A-C and E-H byte-identical", all_match);
    }
    bad += check("out-of-range section rejected",
                 s37_cat_load_section(&io, &map, &state, 8u, &loaded, NULL) != 0);
    bad += check("invalid default section rejected",
                 s37_cat_prepare_overlay(&source, &io, &map, &state, &scratch,
                                         &replacement, 2u, 4u, 8u) != 0);
    replacement.sum ^= 1u;
    bad += check("project with invalid inner checksum cannot be prepared",
                 s37_cat_prepare_overlay(&source, &io, &map, &state, &scratch,
                                         &replacement, 2u, 4u, 0u) != 0);

    bad += check("prepare erase overlay for section H",
                 s37_cat_prepare_erase_overlay(&source, &io, &map, &state, &scratch, 7u) == 0);
    bad += check("commit section H erase overlay",
                 sm_save_stream(&io, &map, s37_cat_overlay_read, &source,
                                S37_CAT_PAYLOAD_SIZE, &state) == 0);
    bad += check("erased section H is absent and default moves to section A",
                 s37_cat_validate(&io, &map, &state, &scratch, &header) == 0 &&
                 header.present_mask == 0x7Fu && header.default_section == 0u &&
                 s37_cat_load_section(&io, &map, &state, 7u, &loaded, NULL) != 0);
    {
        int all_match = 1;
        for (i = 0; i < 7u; ++i)
            all_match &= s37_cat_load_section(&io, &map, &state, (uint8_t)i,
                                              &loaded, NULL) == 0 &&
                         !memcmp(&loaded, &expected[i], sizeof loaded);
        bad += check("erasing H leaves sections A-G byte-identical", all_match);
    }

    return bad ? 1 : 0;
}
