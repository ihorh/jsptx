#include "jsp_classify.h"

#if defined(__ARM_NEON)

#include "jsp_structural_chars.h"

#include <arm_neon.h>

/* NEON has no movemask instruction. This is the standard substitute: weight
   each lane by its bit position within its own byte (twice, once per 8-lane
   half), then pairwise-add each half down to a single byte, since a lane
   holding 0xff or 0x00 contributes its own bit or nothing to that sum. */
static uint16_t movemask16(uint8x16_t v) {
    static const uint8_t bit_weights[16] = {
        0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
        0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
    };
    uint8x16_t weighted = vandq_u8(v, vld1q_u8(bit_weights));

    uint8x8_t low = vget_low_u8(weighted);
    low = vpadd_u8(low, low);
    low = vpadd_u8(low, low);
    low = vpadd_u8(low, low);

    uint8x8_t high = vget_high_u8(weighted);
    high = vpadd_u8(high, high);
    high = vpadd_u8(high, high);
    high = vpadd_u8(high, high);

    return (uint16_t)(((uint16_t)vget_lane_u8(high, 0) << 8) | vget_lane_u8(low, 0));
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
