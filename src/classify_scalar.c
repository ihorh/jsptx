#include "jsp_classify.h"
#include "jsp_structural_chars.h"

uint64_t jsp_classify64_scalar(const uint8_t *p) {
    uint64_t mask = 0;
    for (int i = 0; i < 64; i++) {
        if (jsp_char_is_structural(p[i])) {
            mask |= (uint64_t)1 << i;
        }
    }
    return mask;
}

jsp_char_masks jsp_classify_masks64_scalar(const uint8_t *p) {
    jsp_char_masks masks = {0, 0, 0};
    for (int i = 0; i < 64; i++) {
        uint8_t c = p[i];
        /* Branchless: || always yields exactly 0 or 1, same as (c == x).
           Three independent `if`s here means three independent, data-
           dependent branches per byte instead of zero — measured as a real
           regression (scalar dropped from ~78 to ~130 ns/block) before this
           fix, worse than the two separate branchless loops it replaced. */
        masks.structural |= (uint64_t)jsp_char_is_structural(c) << i;
        masks.quote |= (uint64_t)(c == '"') << i;
        masks.backslash |= (uint64_t)(c == '\\') << i;
    }
    return masks;
}
