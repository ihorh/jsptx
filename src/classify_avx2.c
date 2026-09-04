#include "jsp_classify.h"

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

#endif /* defined(__AVX2__) */
