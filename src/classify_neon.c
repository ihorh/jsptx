#include "jsp_classify.h" // IWYU pragma: keep

#if defined(__ARM_NEON)

#include "jsp_structural_chars.h"

#include <arm_neon.h>

/* NEON has no movemask instruction. This is the standard substitute: weight
   each lane by its bit position within its own byte, the weights repeating
   every 8 lanes so the low group (0-7) and high group (8-15) each fold to
   an independent byte, since a lane holding 0xff or 0x00 contributes its
   own bit or nothing to that sum. Three rounds of a whole-register pairwise
   add (16 lanes -> 8 -> 4 -> 2 meaningful values, each duplicated across
   the register) leave the low group's total in lane 0 and the high group's
   in lane 1. */
static uint16_t movemask16(uint8x16_t v) {
    static const uint8_t bit_weights[16] = {
        0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
        0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
    };
    uint8x16_t weighted = vandq_u8(v, vld1q_u8(bit_weights));
    /* series of pairwise addition: w[0] + w[1] -> w[0],w[8]  ... w[14] + w[15] -> w[7],w[15] */
    weighted = vpaddq_u8(weighted, weighted);
    weighted = vpaddq_u8(weighted, weighted);
    weighted = vpaddq_u8(weighted, weighted);
    /* extracting single u16 can be tricky because of endiannes on some platforms. */
    uint16_t hi = vgetq_lane_u8(weighted, 1);
    uint16_t lo = vgetq_lane_u8(weighted, 0);
    return (uint16_t)(hi << 8) | lo;
}

/* One character's compare-and-accumulate step. */
static inline uint8x16_t merge_eq(uint8x16_t hit, uint8x16_t v, uint8_t ch) {
    uint8x16_t target = vdupq_n_u8(ch);  /* dup: broadcast `ch` to all 16 lanes */
    uint8x16_t eq = vceqq_u8(v, target); /* eq: 0xff per lane where v[i] == ch, else 0x00 */
    return vorrq_u8(hit, eq);            /* or: fold this character's matches into hit */
}

/* Classifies one 16-byte lane: a bitmask whose bit i is set when p[i] is one
   of the seven structural characters, built from one equality compare per
   character, ORed together, then reduced to 16 bits by movemask16. */
static uint16_t classify16(const uint8_t *p) {
    uint8x16_t v = vld1q_u8(p);     /* ld: load 16 octets into NEON vector register */
    uint8x16_t hit = vdupq_n_u8(0); /* dup: initialize hit with all 0s */
#define JSP_OR_EQ(ch) hit = merge_eq(hit, v, (ch));
    JSP_STRUCTURAL_CHARS(JSP_OR_EQ)
#undef JSP_OR_EQ
    return movemask16(hit);
}

uint64_t jsp_classify64_neon(const uint8_t *p) {
    uint64_t mask = 0;
    for (int lane = 0; lane < 4; lane++) {
        mask |= (uint64_t)classify16(p + lane * 16) << (lane * 16);
    }
    return mask;
}

/* One bit per character rather than one bit per class: bit i is jsp_char_masks'
   structural bit i for { } [ ] : , in that order, bit 6 is '"', bit 7 is '\'.
   Every character needs its own bit for the nibble trick below to disambiguate
   it correctly; folding several characters onto one shared bit (say, to save
   the two spare bits this leaves) can make an unrelated character's nibble
   pair alias onto it — verified against exactly that failure before writing
   this table, with '\' (0x5C) and ',' (0x2C) sharing a low nibble. */
#define JSP_BIT_LBRACE (1 << 0)
#define JSP_BIT_RBRACE (1 << 1)
#define JSP_BIT_LBRACKET (1 << 2)
#define JSP_BIT_RBRACKET (1 << 3)
#define JSP_BIT_COLON (1 << 4)
#define JSP_BIT_COMMA (1 << 5)
#define JSP_BIT_QUOTE (1 << 6)
#define JSP_BIT_BACKSLASH (1 << 7)

/* LOW[low nibble of c] & HIGH[high nibble of c] == c's own bit, for any c in
   { } [ ] : , " \; 0 for anything else. Splitting a byte into two 4-bit
   halves and looking each half up in its own 16-entry table, then ANDing the
   results, is what _mm256_shuffle_epi8/vqtbl1q_u8 buys: one instruction does
   16 (NEON) or 32 (AVX2) of these lookups in parallel, rather than one
   compare per character. */
static const uint8x16_t LOW_NIBBLE_TABLE = {
    /* 0x0 */ 0,
    /* 0x1 */ 0,
    /* 0x2 '"' */ JSP_BIT_QUOTE,
    /* 0x3 */ 0,
    /* 0x4 */ 0,
    /* 0x5 */ 0,
    /* 0x6 */ 0,
    /* 0x7 */ 0,
    /* 0x8 */ 0,
    /* 0x9 */ 0,
    /* 0xA ':' */ JSP_BIT_COLON,
    /* 0xB '{' '[' */ JSP_BIT_LBRACE | JSP_BIT_LBRACKET,
    /* 0xC ',' '\' */ JSP_BIT_COMMA | JSP_BIT_BACKSLASH,
    /* 0xD '}' ']' */ JSP_BIT_RBRACE | JSP_BIT_RBRACKET,
    /* 0xE */ 0,
    /* 0xF */ 0,
};

static const uint8x16_t HIGH_NIBBLE_TABLE = {
    /* 0x0 */ 0,
    /* 0x1 */ 0,
    /* 0x2 ',' '"' */ JSP_BIT_COMMA | JSP_BIT_QUOTE,
    /* 0x3 ':' */ JSP_BIT_COLON,
    /* 0x4 */ 0,
    /* 0x5 '[' ']' '\' */ JSP_BIT_LBRACKET | JSP_BIT_RBRACKET | JSP_BIT_BACKSLASH,
    /* 0x6 */ 0,
    /* 0x7 '{' '}' */ JSP_BIT_LBRACE | JSP_BIT_RBRACE,
    /* 0x8 */ 0,
    /* 0x9 */ 0,
    /* 0xA */ 0,
    /* 0xB */ 0,
    /* 0xC */ 0,
    /* 0xD */ 0,
    /* 0xE */ 0,
    /* 0xF */ 0,
};

/* Per-lane tag byte: bit i set iff this byte is exactly the character bit i
   is assigned to above, 0 if it's none of the eight. */
static uint8x16_t classify_tag16(const uint8_t *p) {
    uint8x16_t v = vld1q_u8(p);
    uint8x16_t low_nibble = vandq_u8(v, vdupq_n_u8(0x0F));
    uint8x16_t high_nibble = vshrq_n_u8(v, 4);
    uint8x16_t low_hit = vqtbl1q_u8(LOW_NIBBLE_TABLE, low_nibble);
    uint8x16_t high_hit = vqtbl1q_u8(HIGH_NIBBLE_TABLE, high_nibble);
    return vandq_u8(low_hit, high_hit);
}

/* 0xff per lane where v is nonzero, 0x00 where it's zero — movemask16 needs a
   proper per-lane boolean, not tag16's raw bit pattern, since it extracts a
   bit via AND-with-weight rather than a true/false test. */
static uint8x16_t is_nonzero16(uint8x16_t v) { return vmvnq_u8(vceqq_u8(v, vdupq_n_u8(0))); }

jsp_char_masks jsp_classify_masks64_neon(const uint8_t *p) {
    jsp_char_masks masks = {0, 0, 0};
    for (int lane = 0; lane < 4; lane++) {
        uint8x16_t tag = classify_tag16(p + lane * 16);
        uint8x16_t structural_hit = is_nonzero16(vandq_u8(tag, vdupq_n_u8(0x7F)));
        uint8x16_t quote_hit = is_nonzero16(vandq_u8(tag, vdupq_n_u8(JSP_BIT_QUOTE)));
        uint8x16_t backslash_hit = is_nonzero16(vandq_u8(tag, vdupq_n_u8(JSP_BIT_BACKSLASH)));
        int        shift = lane * 16;
        masks.structural |= (uint64_t)movemask16(structural_hit) << shift;
        masks.quote |= (uint64_t)movemask16(quote_hit) << shift;
        masks.backslash |= (uint64_t)movemask16(backslash_hit) << shift;
    }
    return masks;
}

#undef JSP_BIT_LBRACE
#undef JSP_BIT_RBRACE
#undef JSP_BIT_LBRACKET
#undef JSP_BIT_RBRACKET
#undef JSP_BIT_COLON
#undef JSP_BIT_COMMA
#undef JSP_BIT_QUOTE
#undef JSP_BIT_BACKSLASH

#endif /* defined(__ARM_NEON) */
