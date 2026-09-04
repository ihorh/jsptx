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

#endif /* defined(__ARM_NEON) */
