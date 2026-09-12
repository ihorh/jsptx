#include "jsp_classify.h" // IWYU pragma: keep

#if defined(__ARM_NEON)

#include "jsp_classify_tables.h"

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

/* Per-lane tag byte: bit i set iff this byte is exactly the character bit i
   is assigned to in jsp_classify_tables.h, 0 if it's none of the eight. */
static uint8x16_t classify_tag16(const uint8_t *p) {
    uint8x16_t v = vld1q_u8(p);
    uint8x16_t low_nibble = vandq_u8(v, vdupq_n_u8(0x0F));
    uint8x16_t high_nibble = vshrq_n_u8(v, 4);
    uint8x16_t low_hit = vqtbl1q_u8(vld1q_u8(JSP_LOW_NIBBLE_TABLE), low_nibble);
    uint8x16_t high_hit = vqtbl1q_u8(vld1q_u8(JSP_HIGH_NIBBLE_TABLE), high_nibble);
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
        uint8x16_t struct_hit = is_nonzero16(vandq_u8(tag, vdupq_n_u8(JSP_STRUCTURAL_BITS)));
        uint8x16_t quote_hit = is_nonzero16(vandq_u8(tag, vdupq_n_u8(JSP_BIT_QUOTE)));
        uint8x16_t backslash_hit = is_nonzero16(vandq_u8(tag, vdupq_n_u8(JSP_BIT_BACKSLASH)));
        int        shift = lane * 16;
        masks.structural |= (uint64_t)movemask16(struct_hit) << shift;
        masks.quote |= (uint64_t)movemask16(quote_hit) << shift;
        masks.backslash |= (uint64_t)movemask16(backslash_hit) << shift;
    }
    return masks;
}

#endif /* defined(__ARM_NEON) */
