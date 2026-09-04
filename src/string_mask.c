#include "jsp_string_mask.h"

uint64_t
jsp_string_mask(const unsigned char *block, uint64_t structural_mask, jsp_string_state *state) {
    bool     in_string = state->in_string;
    bool     in_backslash_run = state->in_backslash_run;
    uint64_t bits_to_clear = 0;

    for (int i = 0; i < 64; i++) {
        unsigned char c = block[i];

        bool is_real_quote = (c == '"') && !in_backslash_run;
        if (in_string && !is_real_quote) {
            bits_to_clear |= (uint64_t)1 << i;
        }
        if (is_real_quote) {
            in_string = !in_string;
        }

        in_backslash_run = (c == '\\') ? !in_backslash_run : false;
    }

    state->in_string = in_string;
    state->in_backslash_run = in_backslash_run;
    return structural_mask & ~bits_to_clear;
}
