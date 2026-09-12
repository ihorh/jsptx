#ifndef JSP_BITS_H
#define JSP_BITS_H

#include <stdint.h>

/* Two bit-parallel primitives over a 64-bit word, each replacing a serial
   walk over 64 positions. Neither knows anything about JSON: a caller turns
   bytes into bits first, and reads meaning back out of the result. Both are
   header-only so they inline into the loop that calls them. */

#define JSP_EVEN_BITS 0x5555555555555555ULL
#define JSP_ODD_BITS 0xAAAAAAAAAAAAAAAAULL

/* Within every run of consecutive set bits, returns the bits at odd
   positions counting from the run's own start: the 1st, 3rd, 5th and so on.
   starts_inverted flips that for a run that begins at bit 0, which is what a
   run continuing from the previous word needs.

   jsptx calls this on the backslash mask, where a run's odd positions are the
   backslashes that escape the byte after them rather than being escaped
   themselves. */
static inline uint64_t jsp_bits_run_parity(uint64_t bits, _Bool starts_inverted) {
    /* Computed via the classic "spread a run's start through its extent"
       trick: adding a single 1-bit at a run's start to the run's own 1-bits
       ripples a carry through every subsequent 1-bit until the run's
       terminating 0, so XORing that sum back against the original mask
       recovers exactly that run's span. Splitting run starts by their own
       bit's parity before the add, then re-selecting by bit parity after,
       turns "spread" into "alternate": a run starting on an even bit lands
       its odd-count positions on even bits, and the same for odd. */
    uint64_t start = bits & ~(bits << 1);
    uint64_t start_even = start & JSP_EVEN_BITS;
    uint64_t start_odd = start & JSP_ODD_BITS;

    uint64_t run_even = bits & ((bits + start_even) ^ bits);
    uint64_t run_odd = bits & ((bits + start_odd) ^ bits);

    uint64_t parity = (run_even & JSP_EVEN_BITS) | (run_odd & JSP_ODD_BITS);

    if (starts_inverted && (bits & 1)) {
        /* Bit 0 continues a run that started before this word, so its parity,
           and every bit after it in that same run, is inverted from what a
           fresh start at bit 0 would give. ~bits == 0 means the whole word is
           one run continuing past bit 63. */
        uint64_t first_run =
            (~bits == 0) ? ~(uint64_t)0 : (((uint64_t)1 << __builtin_ctzll(~bits)) - 1);
        parity ^= first_run;
    }

    return parity;
}

/* Turns a set of toggle bits into the inclusive span of the region each
   toggle turns on. Bit i is 1 iff position i is inside a toggled-on region,
   with each toggle's own bit already reflecting the state it switches to:
   the toggle that turns a region on has bit 1, the one that turns it back
   off has bit 0. starts_on is the state carried in before bit 0.

   jsptx calls this on the mask of real quotes, so the spans it returns are
   the stream's strings, each including its opening quote. */
static inline uint64_t jsp_bits_prefix_xor(uint64_t toggles, _Bool starts_on) {
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

#endif /* JSP_BITS_H */
