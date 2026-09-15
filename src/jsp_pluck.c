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

/* The depth records sit at: one level in while the outermost open container
   is an array, since every top-level array unwraps, and 0 otherwise. */
static unsigned record_depth_(const jsp_pluck_state *state) {
    return jsp_nesting_outermost_is_array(&state->nesting) ? 1u : 0u;
}

/* Whether a string starting here would be a key: one right after '{', or after
   ',' inside an object, rather than after ':'. */
static bool key_comes_next_(const jsp_pluck_state *state) {
    return state->last_structural == '{' ||
           (state->last_structural == ',' && jsp_nesting_innermost_is_object(&state->nesting));
}

/* Whether a value starting here is the one the path picks: every segment
   matched, and the walk sits where the last key's value begins. For "." that
   is simply a record's start. */
static bool at_value_(const jsp_pluck_state *state) {
    unsigned n = state->path.segments;
    return state->segments_matched == n && !key_comes_next_(state) &&
           jsp_nesting_depth(&state->nesting) == record_depth_(state) + n;
}

/* Whether a string starting here is a key the next segment could match: one
   directly inside the object that the matched segments lead to. */
static bool at_key_(const jsp_pluck_state *state) {
    return state->segments_matched < state->path.segments && key_comes_next_(state) &&
           jsp_nesting_depth(&state->nesting) ==
               record_depth_(state) + state->segments_matched + 1;
}

/* Starts a value at the byte the caller just found, entering phase. Every
   value after the first gets the separator between it and the one before. */
static int begin_value_(jsp_pluck_state *state, jsp_pluck_phase phase, int out_fd) {
    state->phase = phase;
    if (!state->emitted_any) {
        state->emitted_any = true;
        return 0;
    }
    return jsp_write_jstr(out_fd, state->separator);
}

/* Handles one BYTES token: octets with no structural one among them. A string or
   container value's content needs no byte-level look, since only the value's
   own terminating mask bit can end it, so the whole token goes out in one
   write. A key's octets are compared against the segment, and a skipped
   string's are dropped. Otherwise the token holds bare scalars between
   whitespace, and each pass of the loop emits or drops one of them. A scalar
   reaching the token's end may continue into the next one, so it stays open. */
static int push_bytes_(jsp_pluck_state *state, jsp_slice_u8 bytes, int out_fd) {
    if (state->phase == JSP_PLUCK_STRING || state->phase == JSP_PLUCK_CONTAINER) {
        return jsp_write_all(out_fd, bytes.ptr, bytes.len);
    }
    if (state->phase == JSP_PLUCK_SKIP_STRING) {
        return 0;
    }
    if (state->phase == JSP_PLUCK_KEY) {
        jstr seen = {(const char *)bytes.ptr, (ptrdiff_t)bytes.len};
        if (!jstr_starts_with(state->segment_text_left, seen)) {
            state->phase = JSP_PLUCK_SKIP_STRING; /* differed: the rest of it cannot match */
            return 0;
        }
        state->segment_text_left = jstr_after(state->segment_text_left, seen.len);
        return 0;
    }

    jsp_slice_u8 rest = bytes;
    if (state->phase != JSP_PLUCK_SCALAR) {
        rest = jsp_slice_u8_after(rest, ws_prefix_(rest));
    }
    while (!jsp_slice_u8_empty(rest)) {
        if (state->phase == JSP_PLUCK_BETWEEN && at_value_(state) &&
            begin_value_(state, JSP_PLUCK_SCALAR, out_fd) != 0) {
            return -1;
        }
        size_t n = scalar_prefix_(rest);
        if (state->phase == JSP_PLUCK_SCALAR) {
            if (jsp_write_all(out_fd, rest.ptr, n) != 0) {
                return -1;
            }
            if (n == rest.len) {
                return 0;
            }
            state->phase = JSP_PLUCK_BETWEEN;
        }
        rest = jsp_slice_u8_after(rest, n);
        rest = jsp_slice_u8_after(rest, ws_prefix_(rest));
    }
    return 0;
}

/* Handles one mask bit: one of { } [ ] : , " in stream order, exactly the
   set jsp_nesting_step expects. Every one of them passes through
   jsp_nesting_step once, whatever phase it arrives in. */
static int push_structural_(jsp_pluck_state *state, uint8_t c, int out_fd) {
    bool opening_wrapper = c == '[' && jsp_nesting_depth(&state->nesting) == 0;
    bool at_value = at_value_(state);
    bool at_key = at_key_(state);

    jsp_nesting_result r = jsp_nesting_step(&state->nesting, c);
    if (r != JSP_NESTING_OK) {
        return -1;
    }
    state->last_structural = c;

    if (state->phase == JSP_PLUCK_SCALAR) {
        /* a bare scalar ends at the first whitespace or structural byte;
           push_bytes_ only ever sees the whitespace case, so a scalar butting
           straight up against this mask bit closes here instead */
        state->phase = JSP_PLUCK_BETWEEN;
    }
    if (opening_wrapper) {
        return 0; /* a top-level array's own '[': not a record, stays BETWEEN */
    }

    if (state->phase == JSP_PLUCK_STRING) {
        /* every mask bit strictly inside a string is cleared; only its own
           closing quote survives to reach here */
        state->phase = JSP_PLUCK_BETWEEN;
        return jsp_write_all(out_fd, &c, 1);
    }

    if (state->phase == JSP_PLUCK_KEY) {
        state->segments_matched += jstr_empty(state->segment_text_left);
        state->phase = JSP_PLUCK_BETWEEN;
        return 0;
    }

    if (state->phase == JSP_PLUCK_SKIP_STRING) {
        state->phase = JSP_PLUCK_BETWEEN;
        return 0;
    }

    if (state->phase == JSP_PLUCK_CONTAINER) {
        if (jsp_write_all(out_fd, &c, 1) != 0) {
            return -1;
        }
        unsigned end_depth = record_depth_(state) + state->path.segments;
        if ((c == '}' || c == ']') && jsp_nesting_depth(&state->nesting) == end_depth) {
            state->phase = JSP_PLUCK_BETWEEN;
        }
        return 0;
    }

    /* JSP_PLUCK_BETWEEN, a scalar just closed above included. */
    if (c == '"') {
        if (at_value) {
            if (begin_value_(state, JSP_PLUCK_STRING, out_fd) != 0) {
                return -1;
            }
            return jsp_write_all(out_fd, &c, 1);
        }
        state->phase = JSP_PLUCK_SKIP_STRING;
        if (at_key) {
            state->phase = JSP_PLUCK_KEY;
            state->segment_text_left = jsp_path_segment(&state->path, state->segments_matched);
        }
        return 0;
    }
    if (c == '{' || c == '[') {
        if (at_value) {
            if (begin_value_(state, JSP_PLUCK_CONTAINER, out_fd) != 0) {
                return -1;
            }
            return jsp_write_all(out_fd, &c, 1);
        }
        return 0;
    }

    unsigned depth = jsp_nesting_depth(&state->nesting);
    unsigned record_depth = record_depth_(state);
    if (c == ',') {
        /* the value of the last matched key is over, and its object's next
           key is up for the same segment again */
        if (state->segments_matched > 0 && depth == record_depth + state->segments_matched) {
            state->segments_matched--;
        }
    } else if (c == '}' || c == ']') {
        /* popping above the matched keys' objects unmatches them */
        if (depth < record_depth + state->segments_matched) {
            state->segments_matched = depth - record_depth;
        }
    }
    return 0;
}

int jsp_pluck_push(jsp_pluck_state *state, jsp_token token, int out_fd) {
    if (token.kind == JSP_TOKEN_BYTES) {
        return push_bytes_(state, token.bytes, out_fd);
    }
    return push_structural_(state, token.bytes.ptr[0], out_fd);
}
