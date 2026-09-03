#ifndef JSP_CLASSIFY_H
#define JSP_CLASSIFY_H

#include <stdint.h>

/* Classifies 64 bytes at p, returning a bitmask whose bit i is set when p[i]
   is one of the seven structural characters: { } [ ] : , " */
uint64_t jsp_classify64_scalar(const uint8_t *p);

/* Dispatches to the fastest classifier available for this build: a SIMD
   implementation when the target architecture has one, otherwise the scalar
   version. -Dclassify=scalar forces the scalar version instead, bypassing
   SIMD even when it's available. */
uint64_t jsp_classify64(const uint8_t *p);

#endif /* JSP_CLASSIFY_H */
