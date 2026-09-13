/*
 * jsp_bitstack.h
 * A stack of bits with a fixed capacity, held in one machine word.
 *
 * Copyright (c) 2026 Ihor H.
 * SPDX-License-Identifier: MIT
 */
#ifndef JSP_BITSTACK_H
#define JSP_BITSTACK_H

/*
Bit Stack
=========

A last-in, first-out stack of single bits that knows nothing about JSON.
Zero-initialize for an empty stack.

API
---

    jsp_bitstack_depth(s)     how many bits are pushed
    jsp_bitstack_empty(s)     whether depth is 0
    jsp_bitstack_full(s)      whether depth is JSP_BITSTACK_CAPACITY
    jsp_bitstack_push(s, b)   pushes b; s must not be full
    jsp_bitstack_pop(s)       drops the top bit; s must not be empty
    jsp_bitstack_top(s)       the most recently pushed bit
    jsp_bitstack_at(s, i)     the bit pushed i-th, 0 being the bottom

Notes
-----

Every function is static inline. Callers go through these functions only and
never read the fields, so the word behind them can change without touching a
caller.

The names follow C++'s std::stack, where reading and removing are separate.
top reads the bit, and pop removes it without returning it. Java-style peek
would suggest a pop that returns the value. A caller can check the top bit and
still leave the stack untouched when the check fails.
*/

#include <assert.h>
#include <stdint.h>

#define JSP_BITSTACK_CAPACITY 64u

typedef struct {
    uint64_t bits;
    unsigned depth;
} jsp_bitstack;

static inline unsigned jsp_bitstack_depth(const jsp_bitstack *s) { return s->depth; }

static inline _Bool jsp_bitstack_empty(const jsp_bitstack *s) { return s->depth == 0; }

static inline _Bool jsp_bitstack_full(const jsp_bitstack *s) {
    return s->depth == JSP_BITSTACK_CAPACITY;
}

/* Writes the new bit at position depth and leaves every other bit in place. */
static inline void jsp_bitstack_push(jsp_bitstack *s, _Bool b) {
    assert(!jsp_bitstack_full(s));
    uint64_t keep = ~((uint64_t)1 << s->depth);
    uint64_t bit = (uint64_t)b << s->depth;
    s->bits = (s->bits & keep) | bit;
    s->depth++;
}

/* Leaves the popped bit in the word. The next push overwrites it. */
static inline void jsp_bitstack_pop(jsp_bitstack *s) {
    assert(!jsp_bitstack_empty(s));
    s->depth--;
}

static inline _Bool jsp_bitstack_top(const jsp_bitstack *s) {
    assert(!jsp_bitstack_empty(s));
    return (_Bool)((s->bits >> (s->depth - 1)) & 1u);
}

/* Each bit stays where it was pushed, so the i-th sits at position i. */
static inline _Bool jsp_bitstack_at(const jsp_bitstack *s, unsigned i) {
    assert(i < s->depth);
    return (_Bool)((s->bits >> i) & 1u);
}

#endif /* JSP_BITSTACK_H */
