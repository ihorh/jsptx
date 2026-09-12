#include "jsp_string_mask.h"

#include "jsp_bits.h"

jsp_string_mask_result
jsp_filter_structural_mask(jsp_char_masks masks, jsp_string_state state) {
    /* A backslash always escapes the next byte, unless it is itself the byte
       being escaped by the backslash right before it, which is exactly the
       odd positions of each backslash run. */
    uint64_t parity = jsp_bits_run_parity(masks.backslash, state.trailing_backslash_unpaired);

    /* escaped's bit j equals parity's bit j - 1. parity already means "byte i
       escapes byte i + 1," so shifting it up by one turns that into "byte j
       is escaped." */
    uint64_t escaped = (parity << 1) | (state.trailing_backslash_unpaired ? 1u : 0u);
    uint64_t real_quote_mask = masks.quote & ~escaped;

    /* Each real quote toggles whether we're inside a string; in_string_inclusive
       is the resulting inside/outside mask, with byte 0's starting state
       carried in from the previous block. */
    uint64_t in_string_inclusive = jsp_bits_prefix_xor(real_quote_mask, state.in_string);

    /* A real quote's own byte is never suppressed: an opening quote is not
       yet inside the string it starts, and a closing quote's byte is
       excluded from the content it closes. Masking real_quote_mask out of
       the inclusive toggle is what keeps both sides of that asymmetry right,
       where a plain parity read would only get one of the two. */
    uint64_t bits_to_clear = in_string_inclusive & ~real_quote_mask;

    return (jsp_string_mask_result){
        .structural = masks.structural & ~bits_to_clear,
        .in_string = (in_string_inclusive >> 63) & 1,
        .trailing_backslash_unpaired = (parity >> 63) & 1,
    };
}
