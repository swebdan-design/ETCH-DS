/*
 * ETCH DS -- by Synth Centric
 * Host tests for the SD-card save file (arm9/source/storage.c).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "storage.h"
#include "corrosion.h"

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ETCH's src/storage/fletcher.s, instruction by instruction (8-bit regs) */
static uint16_t fletcher_asm_model(const uint8_t *p, uint32_t n)
{
    uint8_t a, b = 0, c = 0, carry;
    uint32_t i;
    for (i = 0; i < n; i++) {
        unsigned t = (unsigned)p[i] + c;          /* add a, c */
        a = (uint8_t)t; carry = t > 255;
        a = (uint8_t)(a + carry);                 /* adc a, #0 */
        c = a;
        t = (unsigned)a + b;                      /* add a, b */
        a = (uint8_t)t; carry = t > 255;
        a = (uint8_t)(a + carry);
        b = a;
    }
    if ((uint8_t)(c + 1) == 0) c = 0;             /* inc a; jr nz */
    if ((uint8_t)(b + 1) == 0) b = 0;
    return (uint16_t)(c | (b << 8));
}

static void put_gb_slot(uint8_t *sav, int s, const void *data, uint16_t size, uint8_t ver)
{
    uint8_t *b = sav + (s >> 1) * 8192 + (s & 1) * 4096;
    uint16_t sum;
    memcpy(b + 10, data, size);
    sum = fletcher_asm_model(b + 10, size);
    b[0] = 'E'; b[1] = 'T'; b[2] = 'C'; b[3] = 'H'; b[4] = ver; b[5] = 0;
    b[6] = (uint8_t)size; b[7] = (uint8_t)(size >> 8);
    b[8] = (uint8_t)sum; b[9] = (uint8_t)(sum >> 8);
}

static void flip(const char *path, long off)
{
    FILE *f = fopen(path, "r+b");
    int c;
    fseek(f, off, SEEK_SET); c = fgetc(f);
    fseek(f, off, SEEK_SET); fputc(c ^ 0x5A, f);
    fclose(f);
}

int main(void)
{
    static project_t p, q, r;
    ds_ext_t x, y;
    char name[11];
    int i;
    uint8_t rnd[5000];

    for (i = 0; i < 2000; i++) {                              /* checksum == ETCH's */
        int n = rand() % 5000, k;
        for (k = 0; k < n; k++) rnd[k] = (uint8_t)(i & 1 ? 0xFF : rand());
        if (etch_fletcher16(rnd, (uint32_t)n) != fletcher_asm_model(rnd, (uint32_t)n)) {
            CHECK(0, "Fletcher-16 differs from ETCH at n=%d", n);
            break;
        }
    }
    CHECK(1, "Fletcher-16 matches ETCH's assembly");

    storage_path = "../../build/host/etch_ds.sav";
    storage_gb_path = "../../build/host/etch_gb.sav";
    remove(storage_path);
    CHECK(storage_slot_status(0, name) == SLOT_EMPTY, "no file: empty");
    CHECK(storage_last_slot() == SAVE_NONE, "no last slot");

    memset(&p, 0, sizeof p);
    { gen_t g; memset(&g, 0, sizeof g); corr_code_decode(&g, "0F1C90095870"); p.gen = g; corr_generate(&p.pat[0], &g, GEN_ALL); }
    memcpy(p.name, "CORRODE   ", 10);
    p.synth.bpm = 124;
    memset(&x, 0, sizeof x);
    x.acid.cutoff = 77; x.acid.res = 99; x.fx.dly_mix = 31; x.drum_level[5] = 9;
    x.has_drums = 1;
    x.drums[1][2].type = 3; x.drums[1][2].f0 = 1234; x.drums[3][0].amp_ms = 777;   /* edited drums */
    CHECK(storage_save(3, &p, &x), "save slot 4");
    CHECK(storage_slot_status(3, name) == SLOT_OK && !memcmp(name, "CORRODE   ", 10), "status + name");
    CHECK(storage_last_slot() == 3, "last slot remembered");
    memset(&q, 0, sizeof q); memset(&y, 0, sizeof y);
    CHECK(storage_load(3, &q, &y) && !memcmp(&p, &q, sizeof p) && !memcmp(&x, &y, sizeof x), "round trip");
    CHECK(storage_slot_status(4, NULL) == SLOT_EMPTY, "neighbour untouched");

    /* a save interrupted before the magic is written reads as empty, not garbage */
    flip(storage_path, 3 * 16384L);                           /* magic damaged */
    CHECK(storage_slot_status(3, NULL) == SLOT_EMPTY, "interrupted save: empty");
    CHECK(storage_save(3, &p, &x), "re-save");
    /* a damaged byte is caught and the working project is not touched */
    flip(storage_path, 3 * 16384L + 16 + 700);
    CHECK(storage_slot_status(3, NULL) == SLOT_BAD, "corruption detected");
    r = q;
    CHECK(!storage_load(3, &q, &y) && !memcmp(&q, &r, sizeof q), "damaged slot never loads");
    CHECK(storage_save(3, &p, &x), "overwrite heals it");
    CHECK(storage_erase(3) && storage_slot_status(3, NULL) == SLOT_EMPTY, "erase");
    CHECK(storage_last_slot() == SAVE_NONE, "erase forgets last slot");
    for (i = 0; i < DS_SLOTS; i++) { p.name[0] = (char)('A' + i); CHECK(storage_save((uint8_t)i, &p, &x), "fill %d", i); }
    for (i = 0; i < DS_SLOTS; i++) { CHECK(storage_slot_status((uint8_t)i, name) == SLOT_OK && name[0] == 'A' + i, "slot %d", i); }

    /* a version-1 save (before drum designs) still loads, without drums */
    {
        FILE *f;
        uint8_t h[16];
        static uint8_t data[16384];
        uint32_t v1 = (uint32_t)(sizeof(project_t) + offsetof(ds_ext_t, drums));
        uint16_t sum;
        CHECK(storage_save(2, &p, &x), "save for v1 test");
        f = fopen(storage_path, "r+b");
        fseek(f, 2 * 16384L, SEEK_SET); fread(h, 1, 16, f); fread(data, 1, v1, f);
        sum = fletcher_asm_model(data, v1);
        h[4] = 1; h[8] = (uint8_t)v1; h[9] = (uint8_t)(v1 >> 8); h[10] = h[11] = 0;
        h[12] = (uint8_t)sum; h[13] = (uint8_t)(sum >> 8);
        fseek(f, 2 * 16384L, SEEK_SET); fwrite(h, 1, 16, f); fclose(f);
        CHECK(storage_slot_status(2, NULL) == SLOT_OK, "v1 slot accepted");
        CHECK(storage_load(2, &q, &y) && !memcmp(&q, &p, sizeof p) && !y.has_drums &&
              y.acid.cutoff == 77 && y.drums[1][2].f0 == 0, "v1: project + sound, no drum designs");
    }

    /* ETCH Game Boy .sav: v2 slot, v1 slot (no drum voicing), empty, damaged */
    {
        static uint8_t sav[32768];
        FILE *f;
        project_t v1;
        memset(sav, 0, sizeof sav);
        p.name[0] = 'G';
        put_gb_slot(sav, 0, &p, sizeof(project_t), 2);
        v1 = p; v1.name[0] = 'O';
        put_gb_slot(sav, 5, &v1, PROJECT_V1_SIZE, 1);
        put_gb_slot(sav, 6, &p, sizeof(project_t), 2);
        sav[3 * 8192 + 20] ^= 1;                              /* slot 6 damaged */
        f = fopen(storage_gb_path, "wb"); fwrite(sav, 1, sizeof sav, f); fclose(f);
        CHECK(storage_gb_status(0, name) == SLOT_OK && name[0] == 'G', "GB v2 slot");
        CHECK(storage_gb_load(0, &q) && !memcmp(&q, &p, sizeof p), "GB import exact");
        CHECK(storage_gb_status(5, name) == SLOT_OK && name[0] == 'O', "GB v1 slot");
        CHECK(storage_gb_load(5, &q) && q.drum_tune[0] == 8 && q.drum_level[3] == 15 &&
              !memcmp(&q, &v1, PROJECT_V1_SIZE), "GB v1 gets drum defaults");
        CHECK(storage_gb_status(1, NULL) == SLOT_EMPTY, "GB empty");
        CHECK(storage_gb_status(6, NULL) == SLOT_BAD && !storage_gb_load(6, &q), "GB damaged refused");
    }
    printf("test_storage: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
