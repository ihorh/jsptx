#include "jsp_classify.h" // IWYU pragma: keep

#if defined(__AVX2__)

#include "jsp_structural_chars.h"

#include <immintrin.h>

/* One character's compare-and-accumulate step. */
static inline __m256i merge_eq(__m256i hit, __m256i v, uint8_t ch) {
    __m256i target = _mm256_set1_epi8((char)ch); /* ch broadcast to all 32 lanes */
    __m256i eq = _mm256_cmpeq_epi8(v, target);   /* 0xff per lane where v[i] == ch, else 0x00 */
    return _mm256_or_si256(hit, eq);             /* fold this character's matches into hit */
}

/* Classifies one 32-byte lane: a bitmask whose bit i is set when p[i] is one
   of the seven structural characters, built from one equality compare per
   character, ORed together, then reduced to 32 bits by movemask. Unlike
   NEON, AVX2 has a native movemask instruction, so no bit-weight-and-add
   substitute is needed here. */
static uint32_t classify32(const uint8_t *p) {
    __m256i v = _mm256_loadu_si256((const __m256i *)p);
    __m256i hit = _mm256_setzero_si256();
#define JSP_OR_EQ(ch) hit = merge_eq(hit, v, (ch));
    JSP_STRUCTURAL_CHARS(JSP_OR_EQ)
#undef JSP_OR_EQ
    /* _mm256_movemask_epi8 returns a signed int; going through uint32_t
       before widening to uint64_t below avoids sign-extending bit 31 into
       the mask's upper half when lane 1's offset is added in. */
    return (uint32_t)_mm256_movemask_epi8(hit);
}

uint64_t jsp_classify64_avx2(const uint8_t *p) {
    uint64_t mask = 0;
    for (int lane = 0; lane < 2; lane++) {
        mask |= (uint64_t)classify32(p + lane * 32) << (lane * 32);
    }
    return mask;
}

/* One bit per character rather than one bit per class — see
   classify_neon.c's derivation and the ',' / '\' aliasing failure that
   ruled out sharing bits between characters. Kept in sync with that file's
   tables by hand: no header shares them, since the NEON and AVX2 vector
   types aren't the same type. */
#define JSP_BIT_LBRACE (1 << 0)
#define JSP_BIT_RBRACE (1 << 1)
#define JSP_BIT_LBRACKET (1 << 2)
#define JSP_BIT_RBRACKET (1 << 3)
#define JSP_BIT_COLON (1 << 4)
#define JSP_BIT_COMMA (1 << 5)
#define JSP_BIT_QUOTE (1 << 6)
#define JSP_BIT_BACKSLASH (1 << 7)

/* One 16-entry table per nibble, not 32: _mm256_shuffle_epi8 shuffles each
   128-bit half of its input independently, using only the low 4 bits of
   each index byte, so the same 16 bytes need to sit in both halves of the
   32-byte table register. classify_tag32 broadcasts these into both halves
   at lookup time rather than keeping a 32-byte copy of each. */
static const uint8_t LOW_NIBBLE_TABLE16[16] = {
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

static const uint8_t HIGH_NIBBLE_TABLE16[16] = {
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

/* Per-lane tag byte, 32 at a time: bit i set iff this byte is exactly the
   character bit i is assigned to above, 0 if it's none of the eight.
   Splitting each byte into nibbles and shifting the high one down by 4 has
   no dedicated 8-bit shift on AVX2, so this shifts as 16-bit lanes instead
   (leaking the neighbor byte's low bits into each result) and masks that
   off with 0x0F immediately after — the standard substitute. */
static __m256i classify_tag32(const uint8_t *p) {
    __m256i v = _mm256_loadu_si256((const __m256i *)p);
    __m256i low_table =
        _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i *)LOW_NIBBLE_TABLE16));
    __m256i high_table =
        _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i *)HIGH_NIBBLE_TABLE16));
    __m256i nibble_mask = _mm256_set1_epi8(0x0F);
    __m256i low_nibble = _mm256_and_si256(v, nibble_mask);
    __m256i high_nibble = _mm256_and_si256(_mm256_srli_epi16(v, 4), nibble_mask);
    __m256i low_hit = _mm256_shuffle_epi8(low_table, low_nibble);
    __m256i high_hit = _mm256_shuffle_epi8(high_table, high_nibble);
    return _mm256_and_si256(low_hit, high_hit);
}

/* 0xff per lane where v is nonzero, 0x00 where it's zero.
   _mm256_movemask_epi8 reads only each byte's top bit, so tag32's raw
   values (0x01, 0x02, ... one bit set, not necessarily the top one) need
   converting to a proper per-lane boolean first, the same way NEON's
   is_nonzero16 does for vqtbl1q_u8's lack of a native movemask. */
static __m256i is_nonzero32(__m256i v) {
    __m256i is_zero = _mm256_cmpeq_epi8(v, _mm256_setzero_si256());
    return _mm256_xor_si256(is_zero, _mm256_set1_epi8((char)0xFF));
}

jsp_char_masks jsp_classify_masks64_avx2(const uint8_t *p) {
    jsp_char_masks masks = {0, 0, 0};
    for (int lane = 0; lane < 2; lane++) {
        __m256i tag = classify_tag32(p + lane * 32);
        __m256i structural_hit = is_nonzero32(_mm256_and_si256(tag, _mm256_set1_epi8(0x7F)));
        __m256i quote_hit =
            is_nonzero32(_mm256_and_si256(tag, _mm256_set1_epi8(JSP_BIT_QUOTE)));
        __m256i backslash_hit =
            is_nonzero32(_mm256_and_si256(tag, _mm256_set1_epi8((char)JSP_BIT_BACKSLASH)));
        int shift = lane * 32;
        masks.structural |= (uint64_t)(uint32_t)_mm256_movemask_epi8(structural_hit) << shift;
        masks.quote |= (uint64_t)(uint32_t)_mm256_movemask_epi8(quote_hit) << shift;
        masks.backslash |= (uint64_t)(uint32_t)_mm256_movemask_epi8(backslash_hit) << shift;
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

#endif /* defined(__AVX2__) */
