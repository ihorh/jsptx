#include "jsp_string_mask.h"

#define JSP_EVEN_BITS 0x5555555555555555ULL
#define JSP_ODD_BITS 0xAAAAAAAAAAAAAAAAULL

/* Bit i set iff byte i is a backslash ending an odd-length run of
   consecutive backslashes, counting from wherever the run truly started,
   including one that began before this block. carry_in is whether the run
   ending at the byte just before this block, if any, had odd length.

   Bit-parallel via the classic "spread a run's start through its extent"
   trick: adding a single 1-bit at a run's start to the run's own 1-bits
   ripples a carry through every subsequent 1-bit until the run's
   terminating 0, so XORing that sum back against the original mask recovers
   exactly that run's span. Splitting run starts by their own bit's parity
   before the add, then re-selecting by bit parity after, turns "spread" into
   "alternate": a run starting on an even bit lands its odd-count positions
   on even bits, and the same for odd. */
static uint64_t backslash_parity(uint64_t backslash, bool carry_in) {
    uint64_t start = backslash & ~(backslash << 1);
    uint64_t start_even = start & JSP_EVEN_BITS;
    uint64_t start_odd = start & JSP_ODD_BITS;

    uint64_t run_even = backslash & ((backslash + start_even) ^ backslash);
    uint64_t run_odd = backslash & ((backslash + start_odd) ^ backslash);

    uint64_t parity = (run_even & JSP_EVEN_BITS) | (run_odd & JSP_ODD_BITS);

    if (carry_in && (backslash & 1)) {
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

uint64_t jsp_string_mask(uint64_t structural_mask, uint64_t quote_mask, uint64_t backslash_mask,
                         jsp_string_state *state) {
    uint64_t bp = backslash_parity(backslash_mask, state->in_backslash_run);
    /* A byte is escaped iff the run ending just before it (i.e. at the
       preceding byte) is odd, hence the shift: bp's own bit i means "byte i
       is the odd-th backslash of its run," which escapes byte i + 1. bp is
       zero at every non-backslash position by construction, so it alone
       cannot carry "the previous block's run was odd" into this block's
       byte 0 when byte 0 isn't itself a backslash — carry_in is ORed in
       directly to cover that case. */
    uint64_t escaped = (bp << 1) | (state->in_backslash_run ? 1u : 0u);
    uint64_t real_quote_mask = quote_mask & ~escaped;

    /* Prefix XOR over the real-quote mask: bit i is the parity of real
       quotes at or before i, i.e. "inside a string, including this byte's
       own toggle." A doubling shift-XOR computes it in six steps instead of
       64 serial toggles. */
    uint64_t in_string_inclusive = real_quote_mask;
    in_string_inclusive ^= in_string_inclusive << 1;
    in_string_inclusive ^= in_string_inclusive << 2;
    in_string_inclusive ^= in_string_inclusive << 4;
    in_string_inclusive ^= in_string_inclusive << 8;
    in_string_inclusive ^= in_string_inclusive << 16;
    in_string_inclusive ^= in_string_inclusive << 32;
    if (state->in_string) {
        in_string_inclusive = ~in_string_inclusive;
    }

    /* A real quote's own byte is never suppressed: an opening quote is not
       yet inside the string it starts, and a closing quote's byte is
       excluded from the content it closes. Masking real_quote_mask out of
       the inclusive toggle is what keeps both sides of that asymmetry right,
       where a plain parity read would only get one of the two. */
    uint64_t bits_to_clear = in_string_inclusive & ~real_quote_mask;

    state->in_backslash_run = (bp >> 63) & 1;
    state->in_string = (in_string_inclusive >> 63) & 1;

    return structural_mask & ~bits_to_clear;
}
