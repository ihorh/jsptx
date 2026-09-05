#include "jsp_string_mask.h"

#define JSP_EVEN_BITS 0x5555555555555555ULL
#define JSP_ODD_BITS 0xAAAAAAAAAAAAAAAAULL

/* Returns a mask with a bit set at every backslash that escapes the byte
   right after it. A backslash always escapes the next byte, unless it is
   itself the byte being escaped by the backslash right before it.
   trailing_backslash_unpaired is whether the run ending at the byte just
   before this block, if any, had odd length, i.e. whether its last backslash
   escapes into this block rather than being escaped itself. */
static uint64_t backslash_parity(uint64_t backslash, bool trailing_backslash_unpaired) {
    /* Within a run of consecutive backslashes the bit above alternates,
       starting true at the run's first byte. Computed bit-parallel via the
       classic "spread a run's start through its extent" trick: adding a
       single 1-bit at a run's start to the run's own 1-bits ripples a carry
       through every subsequent 1-bit until the run's terminating 0, so
       XORing that sum back against the original mask recovers exactly that
       run's span. Splitting run starts by their own bit's parity before the
       add, then re-selecting by bit parity after, turns "spread" into
       "alternate": a run starting on an even bit lands its odd-count
       positions on even bits, and the same for odd. */
    uint64_t start = backslash & ~(backslash << 1);
    uint64_t start_even = start & JSP_EVEN_BITS;
    uint64_t start_odd = start & JSP_ODD_BITS;

    uint64_t run_even = backslash & ((backslash + start_even) ^ backslash);
    uint64_t run_odd = backslash & ((backslash + start_odd) ^ backslash);

    uint64_t parity = (run_even & JSP_EVEN_BITS) | (run_odd & JSP_ODD_BITS);

    if (trailing_backslash_unpaired && (backslash & 1)) {
        /* Byte 0 continues a run that started before this block, so its
           parity, and every byte after it in that same run, is inverted
           from what a fresh start at byte 0 would give. ~backslash == 0
           means the whole block is one run continuing past byte 63. */
        uint64_t first_run = (~backslash == 0)
                                 ? ~(uint64_t)0
                                 : (((uint64_t)1 << __builtin_ctzll(~backslash)) - 1);
        parity ^= first_run;
    }

    return parity;
}

/* Turns a set of toggle bits into the inclusive span of the region each
   toggle turns on. Bit i is 1 iff position i is inside a toggled-on region,
   with each toggle's own bit already reflecting the state it switches to:
   the toggle that turns a region on has bit 1, the one that turns it back
   off has bit 0. starts_on is the state carried in before bit 0. */
static uint64_t bit_toggle_mask(uint64_t toggles, bool starts_on) {
    /* Hillis-Steele prefix XOR scan: six shift-XOR steps instead of 64
       serial toggles. */
    uint64_t v = toggles;
    v ^= v << 1;
    v ^= v << 2;
    v ^= v << 4;
    v ^= v << 8;
    v ^= v << 16;
    v ^= v << 32;
    if (starts_on) {
        v = ~v;
    }
    return v;
}

uint64_t jsp_filter_structural_mask(jsp_char_masks masks, jsp_string_state *state) {
    uint64_t bp = backslash_parity(masks.backslash, state->trailing_backslash_unpaired);
    /* escaped's bit j equals bp's bit j - 1. bp already means "byte i
       escapes byte i + 1," so shifting it up by one turns that into "byte j
       is escaped." */
    uint64_t escaped = (bp << 1) | (state->trailing_backslash_unpaired ? 1u : 0u);
    uint64_t real_quote_mask = masks.quote & ~escaped;

    /* Each real quote toggles whether we're inside a string; in_string_inclusive
       is the resulting inside/outside mask, with byte 0's starting state
       carried in from the previous block. */
    uint64_t in_string_inclusive = bit_toggle_mask(real_quote_mask, state->in_string);

    /* A real quote's own byte is never suppressed: an opening quote is not
       yet inside the string it starts, and a closing quote's byte is
       excluded from the content it closes. Masking real_quote_mask out of
       the inclusive toggle is what keeps both sides of that asymmetry right,
       where a plain parity read would only get one of the two. */
    uint64_t bits_to_clear = in_string_inclusive & ~real_quote_mask;

    state->trailing_backslash_unpaired = (bp >> 63) & 1;
    state->in_string = (in_string_inclusive >> 63) & 1;

    return masks.structural & ~bits_to_clear;
}
