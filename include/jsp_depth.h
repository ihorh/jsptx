#ifndef JSP_DEPTH_H
#define JSP_DEPTH_H

#include <stdint.h>

/* Nesting is bounded at JSP_MAX_DEPTH: RFC 8259 §9 permits an implementation
   to limit it, and a fixed bound holds the parser's memory constant for every
   input rather than growing with hostile ones. At this bound the whole stack
   is one uint64_t. */
#define JSP_MAX_DEPTH 64

/* One structural character's effect on nesting. OK covers ':', ',', '"', and
   every open bracket that did not overflow. BOUNDARY is a close bracket that
   returned depth to zero: one complete top-level value ends there. The three
   ERROR results are the ways a stream can violate bracket matching; the
   caller's own offset for the character that triggered one locates it. */
typedef enum {
    JSP_DEPTH_OK,
    JSP_DEPTH_BOUNDARY,
    JSP_DEPTH_ERROR_OVERFLOW,   /* an open bracket past JSP_MAX_DEPTH */
    JSP_DEPTH_ERROR_MISMATCH,   /* a close bracket does not match its container */
    JSP_DEPTH_ERROR_UNBALANCED, /* a close bracket with no open container */
} jsp_depth_result;

/* Nesting state carried across every structural character in stream order:
   one bit per open container, 1 for an object and 0 for an array, and how
   many of those bits are valid. Zero-initialize for the first character of a
   stream. */
typedef struct {
    uint64_t stack;
    unsigned depth;
} jsp_depth_state;

/* Advances state by one structural character, one of { } [ ] : , " as
   jsp_filter_structural_mask leaves them. ':', ',', and '"' never change
   depth. On an ERROR result, state is left exactly as it was before c: the
   caller stops there rather than continuing to interpret an already-invalid
   stream. */
static inline jsp_depth_result jsp_depth_step(jsp_depth_state *state, uint8_t c) {
    switch (c) {
    case '{':
    case '[':
        if (state->depth >= JSP_MAX_DEPTH) {
            return JSP_DEPTH_ERROR_OVERFLOW;
        }
        state->stack = (state->stack << 1) | (uint64_t)(c == '{');
        state->depth++;
        return JSP_DEPTH_OK;
    case '}':
    case ']':
        if (state->depth == 0) {
            return JSP_DEPTH_ERROR_UNBALANCED;
        }
        if ((unsigned)(state->stack & 1) != (unsigned)(c == '}')) {
            return JSP_DEPTH_ERROR_MISMATCH;
        }
        state->stack >>= 1;
        state->depth--;
        return state->depth == 0 ? JSP_DEPTH_BOUNDARY : JSP_DEPTH_OK;
    default:
        return JSP_DEPTH_OK;
    }
}

#endif /* JSP_DEPTH_H */
