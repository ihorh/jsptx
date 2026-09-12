#include "jsp_scan.h"

#include "jsp_bits.h"
#include "jsp_classify.h"
#include "jsp_reader.h"

#include <assert.h>

/* The low len bits set. Written as a right shift because 1 << 64 is undefined
   where ~0 >> 0 is not, and len reaches JSP_SCAN_BLOCK on every full block. */
static inline uint64_t low_bits(size_t len) { return ~(uint64_t)0 >> (JSP_SCAN_BLOCK - len); }

/* Moves all three parts of the window past n octets at once, which is what
   holds its invariant true. */
static inline void advance(jsp_scan_window *w, size_t n) {
    w->rest.ptr += n;
    w->rest.len -= n;
    w->offset += n;
    /* A token covering a whole block leaves n at JSP_SCAN_BLOCK, where the shift
       would be undefined. Such a token ended at rest.len rather than at a set
       bit, so the mask holds nothing worth keeping either way. */
    w->mask = n < JSP_SCAN_BLOCK ? w->mask >> n : 0;
}

void jsp_scan_init(jsp_scan *s, int fd, jsp_buf_u8 buf, jsp_trace trace) {
    jsp_reader_init(&s->reader, fd, buf, JSP_SCAN_BLOCK);
    s->trace = trace;
    s->carry = (jsp_scan_carry){0};
    s->window = (jsp_scan_window){jsp_slice_u8_make(NULL, 0), 0, 0};
}

/* The next token from the window, which the caller holds non-empty. A set bit
   at rest.ptr[0] makes that octet structural; otherwise the token reaches the
   next set bit, or the block's end when none is left. */
static jsp_scan_result jsp_scan_emit(jsp_scan *s) {
    jsp_scan_window *w = &s->window;
    size_t           edge = w->mask != 0 ? (size_t)__builtin_ctzll(w->mask) : w->rest.len;

    jsp_token token = {
        .kind = edge > 0 ? JSP_TOKEN_BYTES : JSP_TOKEN_STRUCTURAL,
        .bytes = jsp_slice_u8_make(w->rest.ptr, edge > 0 ? edge : 1),
        .offset = w->offset,
    };
    advance(w, token.bytes.len);
    return (jsp_scan_result){JSP_SCAN_TOKEN, token};
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

            /* a run's odd positions escape the next octet; even ones are escaped themselves */
            uint64_t parity = jsp_bits_run_parity(backslash, carry.trailing_backslash_unpaired);
            /* parity marks the escaping octet, so the escaped one sits a position up */
            uint64_t escaped = (parity << 1) | (carry.trailing_backslash_unpaired ? 1u : 0u);
            /* only an unescaped quote bounds a string */
            uint64_t quotes = chars.quote & ~escaped;
            uint64_t spans = jsp_bits_prefix_xor(quotes, carry.in_string);
            /* both of a string's own quotes sit outside the content they bound */
            uint64_t inside = spans & ~quotes;

            s->carry.trailing_backslash_unpaired = (parity >> 63) & 1;
            s->carry.in_string = (spans >> 63) & 1;

            /* structural octets inside a string are content; low_bits drops the pad octets */
            uint64_t mask = chars.structural & ~inside & low_bits(next.block.len);

            s->window = (jsp_scan_window){next.block, mask, next.offset};
            if (jsp_trace_block(s->trace, next.offset, next.block, mask) != 0) {
                return (jsp_scan_result){.status = JSP_SCAN_ERROR};
            }
        }
    }
    return jsp_scan_emit(s);
}
