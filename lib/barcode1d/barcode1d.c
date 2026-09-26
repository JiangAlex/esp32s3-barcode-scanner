/**
 * @file barcode1d.c
 * @brief Lightweight 1D barcode decoder — EAN-13 / UPC-A (Task 1).
 *
 * Algorithm (EAN-13):
 *   1. Binarize the scanline (mid-threshold from local min/max).
 *   2. Extract run-lengths of alternating bars (black) / spaces (white).
 *   3. An EAN-13 symbol is 95 modules: start guard (3) + 6 left digits (7 each)
 *      + center guard (5) + 6 right digits (7 each) + end guard (3).
 *   4. Estimate module width from the guard patterns, then classify each digit
 *      by matching its 4 run-lengths (2 bars + 2 spaces = 7 modules) against the
 *      L / G / R code tables.
 *   5. The parity sequence of the 6 left digits encodes the 13th (leading) digit.
 *   6. Verify the mod-10 checksum.
 *
 * UPC-A is EAN-13 with a leading '0'; we surface it as UPC-A (12 digits) when
 * the decoded EAN-13 starts with 0.
 *
 * Pure C, no heap. Scanline is scanned in both directions implicitly by trying
 * the reversed run-length sequence when the forward attempt fails.
 */

#include "barcode1d.h"
#include <string.h>

// ─── Tunables ────────────────────────────────────────────────────────────────

// EAN-13 digit encodings as 7-bit module patterns (MSB = first module).
// L-code (odd parity), used for right-hand digits as R-code (bitwise complement).
// G-code (even parity) is the reverse of L-code.
static const uint8_t L_CODE[10] = {
    0x0D, 0x19, 0x13, 0x3D, 0x23, 0x31, 0x2F, 0x3B, 0x37, 0x0B
    // 0001101 0011001 0010011 0111101 0100011 0110001 0101111 0111011 0110111 0001011
};

// First-digit parity table: 1 = odd (L), 0 = even (G) for the 6 left digits.
static const uint8_t PARITY[10] = {
    0x3F, 0x34, 0x32, 0x31, 0x2C, 0x26, 0x23, 0x2A, 0x29, 0x25
    // 111111 110100 110010 110001 101100 100110 100011 101010 101001 100101
};

// ─── EAN-13 decode via module-grid sampling ─────────────────────────────────
//
// Per-digit run slicing breaks when a G-code digit (bar-first) merges its
// leading bar with the previous element's run. The robust, standard approach is
// to locate the symbol's start/stop guards, derive the module pitch, then
// sample each of the 95 modules at its center. This is immune to run merging.
//
// We binarize to a bool array first (true = bar/black).

// Find the EAN-13 symbol span within the binarized row and decode it.
// bits[]: true=bar. n=width. Returns true on success.
static bool decode_ean13_bits(const bool* bits, int n, int digits[13]) {
    // 1. Find first bar (start of quiet-zone→guard transition).
    int i = 0;
    while (i < n && !bits[i]) i++;
    if (i >= n) return false;
    int first_bar = i;

    // 2. Find last bar.
    int j = n - 1;
    while (j > first_bar && !bits[j]) j--;
    int last_bar = j;

    int span = last_bar - first_bar + 1;
    if (span < 95) return false;               // need at least 1 px/module
    float module = (float)span / 95.0f;
    if (module < 0.9f) return false;

    // 3. Sample a module's value at its center. Module k center x:
    //    x = first_bar + (k + 0.5) * module
    // Returns 1 if bar (black) at center, else 0.
    #define MOD_AT(k) (bits[(int)(first_bar + ((k) + 0.5f) * module)] ? 1 : 0)

    // 4. Validate guards: start 101 (modules 0..2), center 01010 (45..49),
    //    end 101 (92..94).
    if (!(MOD_AT(0) && !MOD_AT(1) && MOD_AT(2))) return false;
    if (!(!MOD_AT(45) && MOD_AT(46) && !MOD_AT(47) && MOD_AT(48) && !MOD_AT(49))) return false;
    if (!(MOD_AT(92) && !MOD_AT(93) && MOD_AT(94))) return false;

    // 5. Left 6 digits at module offsets 3 + d*7 .. (7 modules each).
    uint8_t parity_bits = 0;
    for (int d = 0; d < 6; d++) {
        int base = 3 + d * 7;
        uint8_t pat = 0;
        for (int b = 0; b < 7; b++) {
            if (MOD_AT(base + b)) pat |= (uint8_t)(1 << (6 - b));
        }
        int val = -1, par = 0;
        for (int t = 0; t < 10; t++) {
            if (pat == L_CODE[t]) { val = t; par = 1; break; }   // L (odd)
            uint8_t g = 0;
            for (int bb = 0; bb < 7; bb++) if (L_CODE[t] & (1 << bb)) g |= (uint8_t)(1 << (6 - bb));
            if (pat == g) { val = t; par = 0; break; }           // G (even)
        }
        if (val < 0) return false;
        digits[1 + d] = val;
        parity_bits = (uint8_t)((parity_bits << 1) | (par ? 1 : 0));
    }

    // 6. Right 6 digits at module offsets 50 + d*7 (R-code = ~L).
    for (int d = 0; d < 6; d++) {
        int base = 50 + d * 7;
        uint8_t pat = 0;
        for (int b = 0; b < 7; b++) {
            if (MOD_AT(base + b)) pat |= (uint8_t)(1 << (6 - b));
        }
        int val = -1;
        for (int t = 0; t < 10; t++) {
            uint8_t r = (uint8_t)((~L_CODE[t]) & 0x7F);
            if (pat == r) { val = t; break; }
        }
        if (val < 0) return false;
        digits[7 + d] = val;
    }
    #undef MOD_AT

    // 7. Leading digit from parity pattern of the 6 left digits.
    int lead = -1;
    for (int k = 0; k < 10; k++) if (PARITY[k] == parity_bits) { lead = k; break; }
    if (lead < 0) return false;
    digits[0] = lead;

    // 8. Mod-10 checksum: even index weight 1, odd index weight 3.
    int sum = 0;
    for (int k = 0; k < 12; k++) sum += digits[k] * ((k % 2 == 0) ? 1 : 3);
    int check = (10 - (sum % 10)) % 10;
    if (check != digits[12]) return false;

    return true;
}

// ─── Code128 decode ──────────────────────────────────────────────────────────
//
// Each Code128 symbol is 6 elements (bar,space,bar,space,bar,space) = 11
// modules, except Stop which is 7 elements = 13 modules. A symbol is identified
// by its element-width pattern (each element 1..4 modules). We extract
// run-lengths, normalize each group of 6 (or 7 for stop) elements to module
// widths, and match against the canonical table.

// Canonical patterns for symbol values 0..106 (106 = Stop, 7 elements).
static const char* C128[107] = {
    "212222", "222122", "222221", "121223", "121322", "131222", "122213", "122312",
    "132212", "221213", "221312", "231212", "112232", "122132", "122231", "113222",
    "123122", "123221", "223211", "221132", "221231", "213212", "223112", "312131",
    "311222", "321122", "321221", "312212", "322112", "322211", "212123", "212321",
    "232121", "111323", "131123", "131321", "112313", "132113", "132311", "211313",
    "231113", "231311", "112133", "112331", "132131", "113123", "113321", "133121",
    "313121", "211331", "231131", "213113", "213311", "213131", "311123", "311321",
    "331121", "312113", "312311", "332111", "314111", "221411", "431111", "111224",
    "111422", "121124", "121421", "141122", "141221", "112214", "112412", "122114",
    "122411", "142112", "142211", "241211", "221114", "413111", "241112", "134111",
    "111242", "121142", "121241", "114212", "124112", "124211", "411212", "421112",
    "421211", "212141", "214121", "412121", "111143", "111341", "131141", "114113",
    "114311", "411113", "411311", "113141", "114131", "311141", "411131", "211412",
    "211214", "211232", "2331112",
};

#define C128_START_A 103
#define C128_START_B 104
#define C128_START_C 105
#define C128_STOP    106

// Match 6 element widths (module counts) against the 6-element patterns.
// Returns symbol value 0..105, or -1.
static int c128_match6(const int* w6) {
    for (int v = 0; v < 106; v++) {          // 0..105 are 6-element patterns
        const char* p = C128[v];
        bool ok = true;
        for (int k = 0; k < 6; k++) {
            if ((p[k] - '0') != w6[k]) { ok = false; break; }
        }
        if (ok) return v;
    }
    return -1;
}

// Decode Code128 from binarized bits (true=bar). Fills out->text on success.
static bool decode_code128_bits(const bool* bits, int n, bc1d_result_t* out) {
    // Extract run-lengths (alternating). Skip leading quiet zone (spaces).
    int i = 0;
    while (i < n && !bits[i]) i++;            // skip to first bar
    if (i >= n) return false;
    int startpos = i;

    // Collect runs from the first bar.
    int runs[512];
    int nr = 0;
    bool cur = true;                          // first run is a bar
    int len = 0;
    for (int x = startpos; x < n; x++) {
        if (bits[x] == cur) { len++; }
        else { if (nr < 512) runs[nr++] = len; else break; cur = bits[x]; len = 1; }
    }
    if (len > 0 && nr < 512) runs[nr++] = len;

    // Need at least start(6) + checksum(6) + stop(7) = 19 runs.
    if (nr < 19) return false;

    // Estimate module width from the first 6 elements (start char = 11 modules).
    int sum6 = 0;
    for (int k = 0; k < 6; k++) sum6 += runs[k];
    float module = (float)sum6 / 11.0f;
    if (module < 0.8f) return false;

    // Helper: normalize 6 runs at offset to module counts (each clamped 1..4).
    // Returns false if the group doesn't total 11 modules.
    int pos = 0;
    int values[64];
    int nvals = 0;

    // Parse symbols until we hit Stop.
    while (pos + 6 <= nr) {
        // Check for Stop (7 elements) first when enough runs remain.
        if (pos + 7 <= nr) {
            int w7[7], s7 = 0;
            for (int k = 0; k < 7; k++) {
                int c = (int)((float)runs[pos + k] / module + 0.5f);
                if (c < 1) c = 1;
                if (c > 4) c = 4;
                w7[k] = c; s7 += c;
            }
            if (s7 == 13) {
                bool stop = true;
                for (int k = 0; k < 7; k++) if ((C128[C128_STOP][k] - '0') != w7[k]) { stop = false; break; }
                if (stop) { break; }        // reached Stop
            }
        }
        int w6[6];
        for (int k = 0; k < 6; k++) {
            int c = (int)((float)runs[pos + k] / module + 0.5f);
            if (c < 1) c = 1;
            if (c > 4) c = 4;
            w6[k] = c;
        }
        int v = c128_match6(w6);
        if (v < 0) return false;
        if (nvals < 64) values[nvals++] = v; else return false;
        pos += 6;

        // Re-estimate module from this symbol (adaptive to width drift).
        int s = 0; for (int k = 0; k < 6; k++) s += runs[pos - 6 + k];
        module = (module * 3.0f + (float)s / 11.0f) / 4.0f;
    }

    // Need at least start + 1 data + checksum.
    if (nvals < 3) return false;

    int start_val = values[0];
    if (start_val != C128_START_A && start_val != C128_START_B && start_val != C128_START_C)
        return false;

    // Checksum: last data value is the check digit.
    int check = values[nvals - 1];
    long sum = start_val;
    for (int k = 1; k < nvals - 1; k++) sum += (long)values[k] * k;
    if ((int)(sum % 103) != check) return false;

    // Decode values (excluding start and checksum) per code set, with switches.
    char text[48];
    int tlen = 0;
    int set = (start_val == C128_START_A) ? 0 : (start_val == C128_START_B) ? 1 : 2; // A,B,C

    for (int k = 1; k < nvals - 1; k++) {
        int v = values[k];
        if (set == 2) {                       // Code C: pairs of digits 00..99
            if (v <= 99) {
                if (tlen + 2 < 48) { text[tlen++] = (char)('0' + v / 10); text[tlen++] = (char)('0' + v % 10); }
            } else if (v == 100) { set = 1; }  // Code B
            else if (v == 101) { set = 0; }    // Code A
            else { /* FNC/shift: ignore */ }
        } else {                              // Code A or B
            if (v < 96) {
                // Value→ASCII: Code B v0..95 → ' '..'~' (32..127). Code A similar
                // but 64..95 map to control; for product labels treat as B-style.
                char c = (char)(v + 32);
                if (tlen + 1 < 48) text[tlen++] = c;
            } else if (v == 99) { set = 2; }   // Code C
            else if (v == 100) { set = 1; }    // Code B
            else if (v == 101) { set = 0; }    // Code A
            else { /* FNC/shift: ignore */ }
        }
    }
    if (tlen == 0) return false;
    text[tlen] = '\0';

    out->type = BC1D_CODE_128;
    memcpy(out->text, text, (size_t)tlen + 1);
    out->length = tlen;
    out->ok = true;
    return true;
}

// ─── Public API ──────────────────────────────────────────────────────────────

bool bc1d_decode_line(const uint8_t* luma, int width, bc1d_result_t* out) {
    if (!luma || width < 95 || !out) return false;
    out->ok = false;
    out->type = BC1D_NONE;
    out->text[0] = '\0';
    out->length = 0;

    // Binarize using the global mid-threshold from min/max.
    uint8_t lo = 255, hi = 0;
    for (int i = 0; i < width; i++) {
        if (luma[i] < lo) lo = luma[i];
        if (luma[i] > hi) hi = luma[i];
    }
    if (hi - lo < 40) return false;         // too little contrast
    uint8_t th = (uint8_t)((lo + hi) / 2);

    // Stack buffer for the binarized row. Cap at a sane max to bound stack use;
    // callers feed a single row (<= ~1024 px for our SVGA use).
    static bool s_bits[1024];
    if (width > 1024) width = 1024;
    for (int i = 0; i < width; i++) s_bits[i] = (luma[i] < th);   // true = bar

    int digits[13];

    // Forward.
    if (decode_ean13_bits(s_bits, width, digits)) {
        char buf[16];
        for (int i = 0; i < 13; i++) buf[i] = (char)('0' + digits[i]);
        buf[13] = '\0';
        if (digits[0] == 0) {
            out->type = BC1D_UPC_A;
            memcpy(out->text, buf + 1, 12); out->text[12] = '\0'; out->length = 12;
        } else {
            out->type = BC1D_EAN_13;
            memcpy(out->text, buf, 13); out->text[13] = '\0'; out->length = 13;
        }
        out->ok = true;
        return true;
    }

    // Reversed (barcode scanned right-to-left).
    static bool s_rev[1024];
    for (int i = 0; i < width; i++) s_rev[i] = s_bits[width - 1 - i];
    if (decode_ean13_bits(s_rev, width, digits)) {
        char buf[16];
        for (int i = 0; i < 13; i++) buf[i] = (char)('0' + digits[i]);
        buf[13] = '\0';
        if (digits[0] == 0) {
            out->type = BC1D_UPC_A;
            memcpy(out->text, buf + 1, 12); out->text[12] = '\0'; out->length = 12;
        } else {
            out->type = BC1D_EAN_13;
            memcpy(out->text, buf, 13); out->text[13] = '\0'; out->length = 13;
        }
        out->ok = true;
        return true;
    }

    // Code128 (forward, then reversed).
    if (decode_code128_bits(s_bits, width, out)) return true;
    if (decode_code128_bits(s_rev, width, out)) return true;

    return false;
}

// ─── Multi-scanline image wrapper ────────────────────────────────────────────
//
// Product labels are rarely aligned to a single perfect row. Sample N evenly
// spaced horizontal scanlines across the image's vertical extent and return the
// first successful decode. `stride` is the byte width of one row (>= width).
bool bc1d_decode_image(const uint8_t* luma, int width, int height, int stride,
                       int n_lines, bc1d_result_t* out) {
    if (!luma || width < 95 || height < 1 || n_lines < 1 || !out) return false;
    if (n_lines > height) n_lines = height;

    for (int i = 0; i < n_lines; i++) {
        // Evenly distribute lines, biased toward the vertical center where the
        // barcode most likely sits within the viewfinder.
        int y = (int)(((long)(i + 1) * height) / (n_lines + 1));
        if (y < 0) y = 0;
        if (y >= height) y = height - 1;
        const uint8_t* row = luma + (long)y * stride;
        if (bc1d_decode_line(row, width, out)) return true;
    }
    return false;
}

// ─── Symbology name ──────────────────────────────────────────────────────────

const char* bc1d_type_name(bc1d_type_t type) {
    switch (type) {
        case BC1D_EAN_13:   return "EAN-13";
        case BC1D_UPC_A:    return "UPC-A";
        case BC1D_CODE_128: return "Code 128";
        default:            return "Unknown";
    }
}
