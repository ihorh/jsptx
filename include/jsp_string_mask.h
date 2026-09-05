#ifndef JSP_STRING_MASK_H
#define JSP_STRING_MASK_H

#include <stdbool.h>
#include <stdint.h>

/* The two bits that cross a block boundary and a refill: whether the byte
   just past this block starts inside a string, and whether it is escaped by
   an odd-length run of backslashes ending at the block's last byte. Zero-
   initialize for the first block of a stream. */
typedef struct {
    bool in_string;
    bool in_backslash_run;
} jsp_string_state;

/* Turns off structural recognition inside strings. structural_mask, quote_mask,
   and backslash_mask are jsp_classify_masks64's three masks for the same 64
   bytes: no separate scan of the block here, only bit-parallel arithmetic
   on masks already computed. state carries in_string and in_backslash_run
   across calls, one call per block in stream order, and is updated in place
   to the state at the end of this block.

   A real (non-escaped) '"' always survives in the result, since it marks a
   string boundary rather than string content: an opening quote is not yet
   inside the string it starts, and a closing quote's own byte is excluded
   from the content it closes. Every other structural character strictly
   between a real opening and closing quote, and every escaped '"', is
   cleared. Returns the corrected mask. */
uint64_t jsp_string_mask(uint64_t structural_mask, uint64_t quote_mask, uint64_t backslash_mask,
                         jsp_string_state *state);

#endif /* JSP_STRING_MASK_H */
