#ifndef JSP_CLASSIFY_H
#define JSP_CLASSIFY_H

#include <stdint.h>

/* Classifies 64 bytes at p, returning a bitmask whose bit i is set when p[i]
   is one of the seven structural characters: { } [ ] : , " */
uint64_t jsp_classify64_scalar(const uint8_t *p);

#if defined(__ARM_NEON)
/* NEON implementation of the same contract as jsp_classify64_scalar, built
   from four 16-byte loads and four mask extractions. Declared only when
   __ARM_NEON is defined, since arm_neon.h belongs to the compiler rather
   than to a standard. */
uint64_t jsp_classify64_neon(const uint8_t *p);
#endif

#if defined(__AVX2__)
/* AVX2 implementation of the same contract as jsp_classify64_scalar, built
   from two 32-byte loads and two _mm256_movemask_epi8 calls. Declared only
   when __AVX2__ is defined, since immintrin.h belongs to the compiler
   rather than to a standard. */
uint64_t jsp_classify64_avx2(const uint8_t *p);
#endif

/* Dispatches to the fastest classifier available for this build: a SIMD
   implementation when the target architecture has one, otherwise the scalar
   version. -Dclassify=scalar forces the scalar version instead, bypassing
   SIMD even when it's available. */
static inline uint64_t jsp_classify64(const uint8_t *p) {
#if defined(JSP_FORCE_SCALAR)
    return jsp_classify64_scalar(p);
#elif defined(__ARM_NEON)
    return jsp_classify64_neon(p);
#elif defined(__AVX2__)
    return jsp_classify64_avx2(p);
#else
    return jsp_classify64_scalar(p);
#endif
}

/* structural is jsp_classify64's own result: any of the seven structural
   characters. quote and backslash are its '"' and '\' bits pulled out on
   their own, from the same pass over the block rather than a second scan of
   it. */
typedef struct {
    uint64_t structural;
    uint64_t quote;
    uint64_t backslash;
} jsp_char_masks;

/* Same 64 bytes, same structural result as jsp_classify64_scalar, plus quote
   and backslash split out. A plain compare-and-accumulate loop; the SIMD
   versions get theirs from one shuffle-based pass instead of three. */
jsp_char_masks jsp_classify_masks64_scalar(const uint8_t *p);

#if defined(__ARM_NEON)
/* NEON implementation of the same contract as jsp_classify_masks64_scalar,
   via a 4-bit-nibble lookup table rather than one compare per character:
   see src/classify_neon.c for the derivation. */
jsp_char_masks jsp_classify_masks64_neon(const uint8_t *p);
#endif

#if defined(__AVX2__)
/* AVX2 implementation of the same contract as jsp_classify_masks64_scalar,
   via the same nibble-lookup approach as jsp_classify_masks64_neon. */
jsp_char_masks jsp_classify_masks64_avx2(const uint8_t *p);
#endif

/* Dispatches the same way jsp_classify64 does. */
static inline jsp_char_masks jsp_classify_masks64(const uint8_t *p) {
#if defined(JSP_FORCE_SCALAR)
    return jsp_classify_masks64_scalar(p);
#elif defined(__ARM_NEON)
    return jsp_classify_masks64_neon(p);
#elif defined(__AVX2__)
    return jsp_classify_masks64_avx2(p);
#else
    return jsp_classify_masks64_scalar(p);
#endif
}

#endif /* JSP_CLASSIFY_H */
