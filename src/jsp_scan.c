#include "jsp_scan.h"

#include "jsp_bits.h"
#include "jsp_classify.h"
#include "jsp_reader.h"
#include "jsp_slice_u8.h"

#include <assert.h>

/* The window with all three of its parts past n octets, which is what holds
   its invariant true. jsp_slice_u8_after rejects an n past rest.len. */
static inline jsp_scan_window jsp_scan_window_after(jsp_scan_window w, size_t n) {
    return (jsp_scan_window){
        .rest = jsp_slice_u8_after(w.rest, n),
        /* shifting by 64 is undefined, and a token that wide empties the window */
        .mask = n < JSP_SCAN_BLOCK ? w.mask >> n : 0,
        .offset = w.offset + n,
    };
}

/* Which of a block's octets JSON is using as structure: the classifier's
   structural characters, less the ones a string quotes, less the padding past
   the block's length. carry arrives holding what the previous block left
   behind, and leaves holding what this one leaves the next. */
static uint64_t structural_mask(jsp_slice_u8 block, jsp_scan_carry *carry) {
    assert(block.len >= 1 && block.len <= JSP_SCAN_BLOCK);

    jsp_char_masks chars = jsp_classify_masks64(block.ptr);
    uint64_t       backslash = chars.backslash;

    /* a run's odd backslashes escape the octet after them */
    uint64_t parity = jsp_bits_run_parity(backslash, carry->trailing_backslash_unpaired);
    /* move each escaping bit onto the octet it escapes */
    uint64_t escaped = (parity << 1) | (carry->trailing_backslash_unpaired ? 1u : 0u);
    /* an escaped quote is string content, so only the rest bound strings */
    uint64_t quotes = chars.quote & ~escaped;
    uint64_t spans = jsp_bits_prefix_xor(quotes, carry->in_string);
    /* a string's own quotes stay outside the content they bound */
    uint64_t inside = spans & ~quotes;

    carry->in_string = (spans >> 63) & 1;
    carry->trailing_backslash_unpaired = (parity >> 63) & 1;

    /* keep the structure standing outside strings and inside the block */
    return chars.structural & ~inside & jsp_bits_low_mask(block.len);
}

void jsp_scan_init(jsp_scan *s, int fd, jsp_buf_u8 buf, jsp_trace trace) {
    jsp_reader_init(&s->reader, fd, buf, JSP_SCAN_BLOCK);
    s->trace = trace;
    s->carry = (jsp_scan_carry){0};
    s->window = (jsp_scan_window){0};
}

jsp_scan_result jsp_scan_next(jsp_scan *s) {
    if (jsp_slice_u8_empty(s->window.rest)) {
        jsp_reader_result next = jsp_reader_next(&s->reader);
        switch (next.status) {
            /* clang-format off */
        case JSP_READER_END:   return (jsp_scan_result){.status = JSP_SCAN_END};
        case JSP_READER_ERROR: return (jsp_scan_result){.status = JSP_SCAN_ERROR};
            /* clang-format on */
        case JSP_READER_BLOCK:
            s->window = (jsp_scan_window){
                .rest = next.block,
                .mask = structural_mask(next.block, &s->carry),
                .offset = next.offset,
            };
            if (jsp_trace_block(s->trace, next.offset, next.block, s->window.mask) != 0) {
                return (jsp_scan_result){.status = JSP_SCAN_ERROR};
            }
        }
    }
    jsp_scan_window w = s->window;
    /* the token reaches the next set bit, or the block's end when none is left */
    size_t edge = w.mask == 0 ? w.rest.len : jsp_bits_trailing_zeros(w.mask);

    jsp_token token;
    token.offset = w.offset;
    if (edge > 0) {
        token.kind = JSP_TOKEN_BYTES;
        token.bytes = jsp_slice_u8_first(w.rest, edge);
    } else {
        /* a set bit at rest.ptr[0] makes that octet structural */
        token.kind = JSP_TOKEN_STRUCTURAL;
        token.bytes = jsp_slice_u8_first(w.rest, 1);
    }
    s->window = jsp_scan_window_after(w, token.bytes.len);
    return (jsp_scan_result){JSP_SCAN_TOKEN, token};
}
