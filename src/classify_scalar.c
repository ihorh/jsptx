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
