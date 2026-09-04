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

/* Dispatches to the fastest classifier available for this build: a SIMD
   implementation when the target architecture has one, otherwise the scalar
   version. -Dclassify=scalar forces the scalar version instead, bypassing
   SIMD even when it's available. */
uint64_t jsp_classify64(const uint8_t *p);

#endif /* JSP_CLASSIFY_H */
