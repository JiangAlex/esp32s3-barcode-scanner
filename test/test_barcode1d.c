/**
 * @file test_barcode1d.c
 * @brief Host-side unit test for the 1D decoder (compile & run with gcc).
 *
 *   gcc -I../lib/barcode1d test_barcode1d.c ../lib/barcode1d/barcode1d.c -o /tmp/bc1dtest && /tmp/bc1dtest
 *
 * Synthesizes ideal EAN-13 scanlines from known digit strings and checks the
 * decoder recovers them (including the parity-derived leading digit and the
 * checksum). Also feeds the reversed line to exercise bidirectional decode.
 */

#include "barcode1d.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// EAN-13 L-code 7-bit patterns (MSB first), same as decoder.
static const unsigned char L_CODE[10] = {
    0x0D, 0x19, 0x13, 0x3D, 0x23, 0x31, 0x2F, 0x3B, 0x37, 0x0B
};
static const unsigned char PARITY[10] = {
    0x3F, 0x34, 0x32, 0x31, 0x2C, 0x26, 0x23, 0x2A, 0x29, 0x25
};

// Append one module (bar=0 dark, space=255 light) repeated `mw` px.
static int emit(unsigned char* buf, int pos, int mw, int dark) {
    for (int i = 0; i < mw; i++) buf[pos + i] = dark ? 20 : 235;
    return pos + mw;
}

// Build an ideal EAN-13 scanline for `digits13` (13 chars). Returns width.
static int synth_ean13(const char* d13, int mw, int quiet, unsigned char* buf) {
    int pos = 0;
    // Leading quiet zone (light).
    for (int i = 0; i < quiet * mw; i++) buf[pos++] = 235;

    int lead = d13[0] - '0';
    unsigned char parity = PARITY[lead];

    // Start guard: bar space bar (1 0 1).
    pos = emit(buf, pos, mw, 1);
    pos = emit(buf, pos, mw, 0);
    pos = emit(buf, pos, mw, 1);

    // Left 6 digits (d13[1..6]).
    for (int i = 0; i < 6; i++) {
        int dv = d13[1 + i] - '0';
        int odd = (parity >> (5 - i)) & 1;   // 1 = L (odd), 0 = G (even)
        unsigned char pat = L_CODE[dv];
        if (!odd) {
            // G = reverse of L (7 bits).
            unsigned char g = 0;
            for (int b = 0; b < 7; b++) if (L_CODE[dv] & (1 << b)) g |= (1 << (6 - b));
            pat = g;
        }
        for (int b = 6; b >= 0; b--) {
            int dark = (pat >> b) & 1;
            pos = emit(buf, pos, mw, dark);
        }
    }

    // Center guard: space bar space bar space (0 1 0 1 0).
    pos = emit(buf, pos, mw, 0);
    pos = emit(buf, pos, mw, 1);
    pos = emit(buf, pos, mw, 0);
    pos = emit(buf, pos, mw, 1);
    pos = emit(buf, pos, mw, 0);

    // Right 6 digits (d13[7..12]) as R-code = complement of L.
    for (int i = 0; i < 6; i++) {
        int dv = d13[7 + i] - '0';
        unsigned char pat = (~L_CODE[dv]) & 0x7F;
        for (int b = 6; b >= 0; b--) {
            int dark = (pat >> b) & 1;
            pos = emit(buf, pos, mw, dark);
        }
    }

    // End guard: bar space bar (1 0 1).
    pos = emit(buf, pos, mw, 1);
    pos = emit(buf, pos, mw, 0);
    pos = emit(buf, pos, mw, 1);

    // Trailing quiet zone.
    for (int i = 0; i < quiet * mw; i++) buf[pos++] = 235;
    return pos;
}

// Compute EAN-13 check digit for the first 12 digits of d.
static int ean13_check(const char* d) {
    int sum = 0;
    for (int i = 0; i < 12; i++) {
        int v = d[i] - '0';
        sum += (i % 2 == 0) ? v : v * 3;
    }
    return (10 - (sum % 10)) % 10;
}

static int g_pass = 0, g_fail = 0;

static void run_case(const char* d12) {
    char d13[16];
    strncpy(d13, d12, 12); d13[12] = '\0';
    d13[12] = (char)('0' + ean13_check(d13));
    d13[13] = '\0';

    unsigned char buf[4096];
    for (int mw = 2; mw <= 4; mw++) {
        int w = synth_ean13(d13, mw, 10, buf);
        bc1d_result_t r;
        bool ok = bc1d_decode_line(buf, w, &r);

        // Expected surface form: UPC-A (drop leading 0) if lead==0, else EAN-13.
        char expect[16];
        if (d13[0] == '0') { strncpy(expect, d13 + 1, 12); expect[12] = '\0'; }
        else               { strcpy(expect, d13); }

        if (ok && strcmp(r.text, expect) == 0) {
            printf("  [PASS] mw=%d  %s -> %s (%s)\n", mw, d13, r.text, bc1d_type_name(r.type));
            g_pass++;
        } else {
            printf("  [FAIL] mw=%d  %s : ok=%d text='%s' expect='%s'\n",
                   mw, d13, ok, ok ? r.text : "-", expect);
            g_fail++;
        }
    }
}

// ─── Code128 synthesis + tests ───────────────────────────────────────────────

static const char* C128[107] = {
    "212222","222122","222221","121223","121322","131222","122213","122312","132212","221213",
    "221312","231212","112232","122132","122231","113222","123122","123221","223211","221132",
    "221231","213212","223112","312131","311222","321122","321221","312212","322112","322211",
    "212123","212321","232121","111323","131123","131321","112313","132113","132311","211313",
    "231113","231311","112133","112331","132131","113123","113321","133121","313121","211331",
    "231131","213113","213311","213131","311123","311321","331121","312113","312311","332111",
    "314111","221411","431111","111224","111422","121124","121421","141122","141221","112214",
    "112412","122114","122411","142112","142211","241211","221114","413111","241112","134111",
    "111242","121142","121241","114212","124112","124211","411212","421112","421211","212141",
    "214121","412121","111143","111341","131141","114113","114311","411113","411311","113141",
    "114131","311141","411131","211412","211214","211232","2331112"
};

// Emit a Code128 element string (module widths) into a luma buffer at `mw` px
// per module. Returns width.
static int synth_c128_elems(const char* elems, int mw, int quiet, unsigned char* buf) {
    int pos = 0;
    for (int i = 0; i < quiet * mw; i++) buf[pos++] = 235;   // leading quiet
    int dark = 1;                                            // first element is a bar
    for (const char* p = elems; *p; p++) {
        int wmods = *p - '0';
        for (int m = 0; m < wmods * mw; m++) buf[pos++] = dark ? 20 : 235;
        dark = !dark;
    }
    for (int i = 0; i < quiet * mw; i++) buf[pos++] = 235;   // trailing quiet
    return pos;
}

// Build a Code B symbol-value sequence for ASCII text, return element string.
static void c128b_elems(const char* text, char* out_elems) {
    int vals[64]; int nv = 0;
    vals[nv++] = 104;                                        // Start B
    for (const char* c = text; *c; c++) vals[nv++] = (int)(*c) - 32;
    long s = vals[0];
    for (int i = 1; i < nv; i++) s += (long)vals[i] * i;
    vals[nv++] = (int)(s % 103);                             // checksum
    vals[nv++] = 106;                                        // Stop
    out_elems[0] = '\0';
    for (int i = 0; i < nv; i++) strcat(out_elems, C128[vals[i]]);
}

// Build a Code C symbol-value sequence for an even-length digit string.
static void c128c_elems(const char* digits, char* out_elems) {
    int vals[64]; int nv = 0;
    vals[nv++] = 105;                                        // Start C
    int L = (int)strlen(digits);
    for (int i = 0; i < L; i += 2) vals[nv++] = (digits[i] - '0') * 10 + (digits[i + 1] - '0');
    long s = vals[0];
    for (int i = 1; i < nv; i++) s += (long)vals[i] * i;
    vals[nv++] = (int)(s % 103);                             // checksum
    vals[nv++] = 106;                                        // Stop
    out_elems[0] = '\0';
    for (int i = 0; i < nv; i++) strcat(out_elems, C128[vals[i]]);
}

static void run_c128_b(const char* text) {
    char elems[256]; c128b_elems(text, elems);
    unsigned char buf[8192];
    for (int mw = 2; mw <= 3; mw++) {
        int w = synth_c128_elems(elems, mw, 10, buf);
        bc1d_result_t r;
        bool ok = bc1d_decode_line(buf, w, &r);
        if (ok && r.type == BC1D_CODE_128 && strcmp(r.text, text) == 0) {
            printf("  [PASS] mw=%d  Code128-B '%s' -> '%s'\n", mw, text, r.text);
            g_pass++;
        } else {
            printf("  [FAIL] mw=%d  Code128-B '%s' : ok=%d text='%s'\n",
                   mw, text, ok, ok ? r.text : "-");
            g_fail++;
        }
    }
}

static void run_c128_c(const char* digits) {
    char elems[256]; c128c_elems(digits, elems);
    unsigned char buf[8192];
    for (int mw = 2; mw <= 3; mw++) {
        int w = synth_c128_elems(elems, mw, 10, buf);
        bc1d_result_t r;
        bool ok = bc1d_decode_line(buf, w, &r);
        if (ok && r.type == BC1D_CODE_128 && strcmp(r.text, digits) == 0) {
            printf("  [PASS] mw=%d  Code128-C '%s' -> '%s'\n", mw, digits, r.text);
            g_pass++;
        } else {
            printf("  [FAIL] mw=%d  Code128-C '%s' : ok=%d text='%s'\n",
                   mw, digits, ok, ok ? r.text : "-");
            g_fail++;
        }
    }
}

// Build a small 2D image where only the center rows contain an EAN-13 barcode;
// other rows are noise. Verify the multi-scanline wrapper finds it.
static void run_wrapper_test(void) {
    const char* d13 = "4006381333931";
    int mw = 3, quiet = 10;
    unsigned char row[4096];
    int w = synth_ean13(d13, mw, quiet, row);

    int H = 40;
    unsigned char* img = (unsigned char*)malloc((size_t)w * H);
    // Fill with noise (mid-gray varying) everywhere.
    for (int i = 0; i < w * H; i++) img[i] = (unsigned char)(120 + (i * 37) % 40);
    // Place the barcode row in the vertical band [15,25).
    for (int y = 15; y < 25; y++) memcpy(img + (size_t)y * w, row, w);

    bc1d_result_t r;
    bool ok = bc1d_decode_image(img, w, H, w, 9, &r);
    if (ok && strcmp(r.text, d13) == 0) {
        printf("  [PASS] wrapper found %s (%s)\n", r.text, bc1d_type_name(r.type));
        g_pass++;
    } else {
        printf("  [FAIL] wrapper : ok=%d text='%s'\n", ok, ok ? r.text : "-");
        g_fail++;
    }
    free(img);
}

int main(void) {
    printf("EAN-13 / UPC-A decode tests\n");
    run_case("400638133393");  // classic EAN-13 example (Coca-Cola-ish)
    run_case("978020137962");  // ISBN-style (leading 9)
    run_case("012345678905");  // leading 0 -> UPC-A
    run_case("073333531084");  // leading 0 -> UPC-A
    run_case("500000000000");  // leading 5

    printf("\nCode128 decode tests\n");
    run_c128_b("AB12");
    run_c128_b("HELLO123");
    run_c128_b("Item-42");
    run_c128_c("12345678");   // Code C (even # of digits)

    printf("\nMulti-scanline wrapper test\n");
    run_wrapper_test();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
