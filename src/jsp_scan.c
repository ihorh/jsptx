#include "jsp_scan.h"

#include "jsp_bits.h"
#include "jsp_classify.h"
#include "jsp_reader.h"
#include "jsp_slice_u8.h"

#include <assert.h>

/* The low len bits set. Written as a right shift because 1 << 64 is undefined
   where ~0 >> 0 is not, and len reaches JSP_SCAN_BLOCK on every full block. */
static inline uint64_t low_bits(size_t len) { return ~(uint64_t)0 >> (JSP_SCAN_BLOCK - len); }

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

void jsp_scan_init(jsp_scan *s, int fd, jsp_buf_u8 buf, jsp_trace trace) {
    jsp_reader_init(&s->reader, fd, buf, JSP_SCAN_BLOCK);
    s->trace = trace;
    s->carry = (jsp_scan_carry){0};
    s->window = (jsp_scan_window){0};
}

jsp_scan_result jsp_scan_next(jsp_scan *s) {
    if (s->window.rest.len == 0) {
        jsp_reader_result next = jsp_reader_next(&s->reader);
        switch (next.status) {
            /* clang-format off */
        case JSP_READER_END:   return (jsp_scan_result){.status = JSP_SCAN_END};
        case JSP_READER_ERROR: return (jsp_scan_result){.status = JSP_SCAN_ERROR};
            /* clang-format on */
        case JSP_READER_BLOCK:
            assert(next.block.len >= 1 && next.block.len <= JSP_SCAN_BLOCK);

            jsp_char_masks chars = jsp_classify_masks64(next.block.ptr);
            uint64_t       backslash = chars.backslash;
            jsp_scan_carry carry = s->carry;

            /* a run's odd backslashes escape the octet after them */
            uint64_t parity = jsp_bits_run_parity(backslash, carry.trailing_backslash_unpaired);
            /* move each escaping bit onto the octet it escapes */
            uint64_t escaped = (parity << 1) | (carry.trailing_backslash_unpaired ? 1u : 0u);
            /* an escaped quote is string content, so only the rest bound strings */
            uint64_t quotes = chars.quote & ~escaped;
            uint64_t spans = jsp_bits_prefix_xor(quotes, carry.in_string);
            /* a string's own quotes stay outside the content they bound */
            uint64_t inside = spans & ~quotes;

            s->carry.trailing_backslash_unpaired = (parity >> 63) & 1;
            s->carry.in_string = (spans >> 63) & 1;

            /* keep the structure standing outside strings and inside the block */
            uint64_t mask = chars.structural & ~inside & low_bits(next.block.len);

            s->window = (jsp_scan_window){next.block, mask, next.offset};
            if (jsp_trace_block(s->trace, next.offset, next.block, mask) != 0) {
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
        token.bytes = jsp_slice_u8_make(w.rest.ptr, edge);
    } else {
        /* a set bit at rest.ptr[0] makes that octet structural */
        token.kind = JSP_TOKEN_STRUCTURAL;
        token.bytes = jsp_slice_u8_make(w.rest.ptr, 1);
    }
    s->window = jsp_scan_window_after(w, token.bytes.len);
    return (jsp_scan_result){JSP_SCAN_TOKEN, token};
}
