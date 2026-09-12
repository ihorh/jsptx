/*
 * jsp_bits.h
 * Bit tricks over one 64-bit word, each replacing a loop over 64 positions.
 *
 * Copyright (c) 2026 Ihor H.
 * SPDX-License-Identifier: MIT
 */
#ifndef JSP_BITS_H
#define JSP_BITS_H

/*
Word-at-a-Time Bit Tricks
=========================

Three primitives over a 64-bit word, none of which knows about JSON.

A caller turns bytes into bits, calls one of these, and reads the meaning back
out of the bits it gets. jsptx calls them on the classifier's masks, where one
word describes 64 octets of input.

API
---

    jsp_bits_trailing_zeros(bits)               index of the lowest set bit
    jsp_bits_run_parity(bits, starts_inverted)  each run's odd-numbered bits
    jsp_bits_prefix_xor(toggles, starts_on)     the span each toggle opens

Notes
-----

Every function is static inline, so it inlines into the loop calling it rather
than costing a call. All three need __builtin_ctzll, and the #error below
rejects a compiler lacking it.
*/

#include <stdint.h>

#if defined(__has_builtin)
#if !__has_builtin(__builtin_ctzll)
#error "jsp_bits needs __builtin_ctzll"
#endif
#elif !defined(__GNUC__)
#error "jsp_bits needs a compiler providing __builtin_ctzll"
#endif

#define JSP_EVEN_BITS 0x5555555555555555ULL
#define JSP_ODD_BITS 0xAAAAAAAAAAAAAAAAULL

/* Returns the index of the lowest set bit. A word with bit 0 set gives 0. An
   empty word gives 64.

   Examples:

    - bits `0b0000_0001  ->  0`   the lowest set bit is bit 0
    - bits `0b0000_0010  ->  1`   one clear bit below the lowest set bit
    - bits `0b0001_1000  ->  3`   bits above the lowest set one are ignored
    - bits `1 << 63      ->  63`  the highest index a non-empty word gives
    - bits `~0           ->  0`   every bit set, so bit 0 is the lowest
    - bits `0            ->  64`  no set bit at all */
static inline unsigned jsp_bits_trailing_zeros(uint64_t bits) {
    /* __builtin_ctzll leaves the empty word undefined, so this answers it */
    return bits != 0 ? (unsigned)__builtin_ctzll(bits) : 64u;
}

/* Keeps the odd-numbered bits of every run of consecutive set bits: the 1st,
   3rd, 5th, and so on. It clears the others. A run of three keeps two bits:

       bits    0b1110   (one run, starting at bit 1)
       result  0b1010   (that run's 1st and 3rd bit)

   starts_inverted says bit 0 continues a run from the previous word. The run
   standing at bit 0 then flips, so its 2nd and 4th bit survive instead.

   How it works. Adding one 1-bit at a run's start ripples a carry through the
   run's own 1-bits and stops at the 0 ending it. XORing that sum against the
   original word therefore marks the whole run. Splitting run starts by their
   own parity before the add, then re-selecting by parity after, turns that
   spread into an alternation.

   jsptx calls this on the backslash mask. A run's odd bits mark the
   backslashes that escape the byte after them. Its even bits mark the
   backslashes that something already escaped. */
static inline uint64_t jsp_bits_run_parity(uint64_t bits, _Bool starts_inverted) {
    uint64_t start = bits & ~(bits << 1);
    uint64_t start_even = start & JSP_EVEN_BITS;
    uint64_t start_odd = start & JSP_ODD_BITS;

    uint64_t run_even = bits & ((bits + start_even) ^ bits);
    uint64_t run_odd = bits & ((bits + start_odd) ^ bits);

    uint64_t parity = (run_even & JSP_EVEN_BITS) | (run_odd & JSP_ODD_BITS);

    /* bit 0 continues a run from the previous word, so that run's whole span
       flips */
    if (starts_inverted && (bits & 1)) {
        /* an empty ~bits gives 64, so a run reaching past bit 63 fills the word */
        parity ^= ~(uint64_t)0 >> (64 - jsp_bits_trailing_zeros(~bits));
    }

    return parity;
}

/* Fills the span that each toggle bit opens, up to the bit closing it. Bit i
   comes back set when an odd number of toggles sits at or below i:

       toggles  0b10010   (one opens at bit 1, the other closes at bit 4)
       result   0b01110   (the span between them)

   The opening toggle's own bit falls inside the span. The closing toggle's bit
   falls outside it. starts_on says a span is already open before bit 0, so the
   first toggle closes that one instead of opening another.

   Six shift-XOR steps cover all 64 positions, which is a Hillis-Steele prefix
   XOR scan.

   jsptx calls this on the mask of real quotes, and each span it returns is one
   string, counting that string's opening quote. */
static inline uint64_t jsp_bits_prefix_xor(uint64_t toggles, _Bool starts_on) {
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
