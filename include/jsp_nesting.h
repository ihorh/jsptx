#ifndef JSP_NESTING_H
#define JSP_NESTING_H

#include "jsp_bitstack.h"

#include <stdint.h>

/* Nesting is bounded at JSP_MAX_DEPTH: RFC 8259 §9 permits an implementation
   to limit it, and a fixed bound holds the parser's memory constant for every
   input rather than growing with hostile ones. The bound is whatever the
   container stack holds. */
#define JSP_MAX_DEPTH JSP_BITSTACK_CAPACITY

/* One structural character's effect on nesting. OK covers ':', ',', '"', and
   every bracket that matched. The three ERROR results are the ways a stream
   can violate bracket matching; the caller's own offset for the character
   that triggered one locates it. */
typedef enum {
    JSP_NESTING_OK,
    JSP_NESTING_ERROR_OVERFLOW,   /* an open bracket past JSP_MAX_DEPTH */
    JSP_NESTING_ERROR_MISMATCH,   /* a close bracket does not match its container */
    JSP_NESTING_ERROR_UNBALANCED, /* a close bracket with no open container */
} jsp_nesting_result;

/* Nesting state carried across every structural character in stream order.
   Zero-initialize for the first character of a stream. */
typedef struct {
    jsp_bitstack containers; /* one bit per open container: 1 object, 0 array */
} jsp_nesting_state;

/* How many containers are open. */
static inline unsigned jsp_nesting_depth(const jsp_nesting_state *state) {
    return jsp_bitstack_depth(&state->containers);
}

/* Whether the innermost open container is an object. False when none is open. */
static inline _Bool jsp_nesting_innermost_is_object(const jsp_nesting_state *state) {
    return !jsp_bitstack_empty(&state->containers) && jsp_bitstack_top(&state->containers);
}

/* Whether the outermost open container is an array. False when none is open. */
static inline _Bool jsp_nesting_outermost_is_array(const jsp_nesting_state *state) {
    return !jsp_bitstack_empty(&state->containers) && !jsp_bitstack_at(&state->containers, 0);
}

/* Advances state by one structural character, one of { } [ ] : , " as
   jsp_scan leaves them, with the ones inside strings already cleared. ':', ',', and '"' never
   change depth. On an ERROR result, state is left exactly as it was before c: the caller stops
   there rather than continuing to interpret an already-invalid stream. */
static inline jsp_nesting_result jsp_nesting_step(jsp_nesting_state *state, uint8_t c) {
    switch (c) {
    case '{':
    case '[':
        if (jsp_bitstack_full(&state->containers)) {
            return JSP_NESTING_ERROR_OVERFLOW;
        }
        jsp_bitstack_push(&state->containers, c == '{');
        return JSP_NESTING_OK;
    case '}':
    case ']':
        if (jsp_bitstack_empty(&state->containers)) {
            return JSP_NESTING_ERROR_UNBALANCED;
        }
        if (jsp_bitstack_top(&state->containers) != (c == '}')) {
            return JSP_NESTING_ERROR_MISMATCH;
        }
        jsp_bitstack_pop(&state->containers);
        return JSP_NESTING_OK;
    default:
        return JSP_NESTING_OK;
    }
}

#endif /* JSP_NESTING_H */
