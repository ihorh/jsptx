#include "jsp_classify.h" // IWYU pragma: keep

#if defined(__AVX2__)

#include "jsp_classify_tables.h"

#include <immintrin.h>

/* Per-lane tag byte, 32 at a time: bit i set iff this byte is exactly the
   character bit i is assigned to in jsp_classify_tables.h, 0 if it's none of
   the eight. Splitting each byte into nibbles and shifting the high one down
   by 4 has no dedicated 8-bit shift on AVX2, so this shifts as 16-bit lanes
   instead (leaking the neighbor byte's low bits into each result) and masks
   that off with 0x0F immediately after — the standard substitute.

   One 16-entry table per nibble, not 32: _mm256_shuffle_epi8 shuffles each
   128-bit half of its input independently, using only the low 4 bits of each
   index byte, so the same 16 bytes need to sit in both halves of the 32-byte
   table register — this broadcasts the shared table into both halves at
   lookup time rather than keeping a 32-byte copy of it. */
static __m256i classify_tag32(const uint8_t *p) {
    __m256i v = _mm256_loadu_si256((const __m256i *)p);
    __m256i low_table =
        _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i *)JSP_LOW_NIBBLE_TABLE));
    __m256i high_table =
        _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i *)JSP_HIGH_NIBBLE_TABLE));
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
        __m256i structural_hit =
            is_nonzero32(_mm256_and_si256(tag, _mm256_set1_epi8(JSP_STRUCTURAL_BITS)));
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

#endif /* defined(__AVX2__) */
