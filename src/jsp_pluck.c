#include "jsp_pluck.h"

#include "jsp_io.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool is_json_ws_(uint8_t c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

/* How many of s's leading octets are JSON whitespace. */
static size_t ws_prefix_(jsp_slice_u8 s) {
    size_t n = 0;
    while (n < s.len && is_json_ws_(s.ptr[n])) {
        n++;
    }
    return n;
}

/* How many of s's leading octets belong to a bare scalar: everything before
   the first whitespace, since a structural octet never reaches a BYTES token. */
static size_t scalar_prefix_(jsp_slice_u8 s) {
    size_t n = 0;
    while (n < s.len && !is_json_ws_(s.ptr[n])) {
        n++;
    }
    return n;
}

/* The stream's shape is decided by its very first non-whitespace byte,
   wherever it turns up: a mask bit in push_structural_, or a bare scalar's
   lead byte in push_bytes_. A '[' there means the whole stream is one
   top-level array to unwrap, and its elements, one depth in, are the
   records; anything else means depth 0 holds them. Every later call is a
   no-op. */
static void latch_shape_(jsp_pluck_state *state, uint8_t c) {
    if (state->shape != JSP_PLUCK_SHAPE_UNKNOWN) {
        return;
    }
    state->shape = c == '[' ? JSP_PLUCK_SHAPE_ARRAY : JSP_PLUCK_SHAPE_VALUES;
}

/* Starts a record at the byte the caller just found, entering phase. Every
   record after the first gets the newline that separates it from the one
   before. */
static int begin_record_(jsp_pluck_state *state, jsp_pluck_phase phase, int out_fd) {
    state->phase = phase;
    if (!state->record_seen) {
        state->record_seen = true;
        return 0;
    }
    return jsp_write_newline(out_fd);
}

/* Handles one BYTES token: octets with no structural one among them. A string or
   container record's content needs no byte-level look, since only the
   record's own terminating mask bit can end it, so the whole token goes out
   in one write. Otherwise the token holds bare scalars between whitespace,
   and each pass of the loop emits one of them. A scalar reaching the token's
   end may continue into the next one, so it stays open. */
static int push_bytes_(jsp_pluck_state *state, jsp_slice_u8 bytes, int out_fd) {
    if (state->phase == JSP_PLUCK_STRING || state->phase == JSP_PLUCK_CONTAINER) {
        return jsp_write_all(out_fd, bytes.ptr, bytes.len);
    }

    jsp_slice_u8 rest = bytes;
    if (state->phase != JSP_PLUCK_SCALAR) {
        rest = jsp_slice_u8_after(rest, ws_prefix_(rest));
    }
    while (!jsp_slice_u8_empty(rest)) {
        latch_shape_(state, rest.ptr[0]); /* never '[': that is always a mask bit */
        if (state->phase == JSP_PLUCK_BETWEEN &&
            begin_record_(state, JSP_PLUCK_SCALAR, out_fd) != 0) {
            return -1;
        }
        size_t n = scalar_prefix_(rest);
        if (jsp_write_all(out_fd, rest.ptr, n) != 0) {
            return -1;
        }
        if (n == rest.len) {
            return 0;
        }
        state->phase = JSP_PLUCK_BETWEEN;
        rest = jsp_slice_u8_after(rest, n);
        rest = jsp_slice_u8_after(rest, ws_prefix_(rest));
    }
    return 0;
}

/* Handles one mask bit: one of { } [ ] : , " in stream order, exactly the
   set jsp_depth_step expects. Every one of them passes through depth
   tracking once, whatever phase it arrives in. */
static int push_structural_(jsp_pluck_state *state, uint8_t c, int out_fd) {
    bool opening_wrapper = state->shape == JSP_PLUCK_SHAPE_UNKNOWN && c == '[';
    latch_shape_(state, c);

    jsp_depth_result r = jsp_depth_step(&state->depth, c);
    if (r == JSP_DEPTH_ERROR_OVERFLOW || r == JSP_DEPTH_ERROR_MISMATCH ||
        r == JSP_DEPTH_ERROR_UNBALANCED) {
        return -1;
    }
    if (opening_wrapper) {
        return 0; /* the stream's own outer '[': not a record, stays BETWEEN */
    }

    if (state->phase == JSP_PLUCK_SCALAR) {
        /* a bare scalar ends at the first whitespace or structural byte;
           push_bytes_ only ever sees the whitespace case, so a scalar butting
           straight up against this mask bit closes here instead */
        state->phase = JSP_PLUCK_BETWEEN;
    }

    if (state->phase == JSP_PLUCK_STRING) {
        /* every mask bit strictly inside a string is cleared; only its own
           closing quote survives to reach here */
        state->phase = JSP_PLUCK_BETWEEN;
        return jsp_write_all(out_fd, &c, 1);
    }

    if (state->phase == JSP_PLUCK_CONTAINER) {
        if (jsp_write_all(out_fd, &c, 1) != 0) {
            return -1;
        }
        unsigned record_depth = state->shape == JSP_PLUCK_SHAPE_ARRAY ? 1u : 0u;
        if ((c == '}' || c == ']') && state->depth.depth == record_depth) {
            state->phase = JSP_PLUCK_BETWEEN;
        }
        return 0;
    }

    /* JSP_PLUCK_BETWEEN, a scalar just closed above included. */
    if (c == '"') {
        if (begin_record_(state, JSP_PLUCK_STRING, out_fd) != 0) {
            return -1;
        }
        return jsp_write_all(out_fd, &c, 1);
    }
    if (c == '{' || c == '[') {
        if (begin_record_(state, JSP_PLUCK_CONTAINER, out_fd) != 0) {
            return -1;
        }
        return jsp_write_all(out_fd, &c, 1);
    }
    /* ':', ',', or an already depth-checked '}'/']': the separator or
       closer between records, or the unwrapped array's own bracket. None
       of them belong to a record's own bytes. */
    return 0;
}

int jsp_pluck_push(jsp_pluck_state *state, jsp_token token, int out_fd) {
    if (token.kind == JSP_TOKEN_BYTES) {
        return push_bytes_(state, token.bytes, out_fd);
    }
    return push_structural_(state, token.bytes.ptr[0], out_fd);
}
