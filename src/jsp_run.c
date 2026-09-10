#define _POSIX_C_SOURCE 200809L

#include "jsp_run.h"

#include "jsp_block.h"
#include "jsp_classify.h"
#include "jsp_io.h"
#include "jsp_pluck.h"
#include "jsp_string_mask.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* One classification covers JSP_BLOCK bytes, and padding the final partial
   block writes up to that many octets past its start. */
#define JSP_BLOCK 64
#define JSP_PAD JSP_BLOCK

static size_t round_up_block(size_t n) {
    if (n < JSP_BLOCK) {
        return JSP_BLOCK;
    }
    return (n + JSP_BLOCK - 1) / JSP_BLOCK * JSP_BLOCK;
}

/* One line per set bit in mask: "<offset + bit>\t<block.bytes[bit]>\n". */
static int emit_offsets(int out_fd, uint64_t offset, jsp_block block, uint64_t mask) {
    while (mask != 0) {
        int bit = __builtin_ctzll(mask);
        mask &= mask - 1;

        char line[32];
        int  len = snprintf(line, sizeof(line), "%" PRIu64 "\t%c\n", offset + (uint64_t)bit,
                            block.bytes[bit]);
        if (jsp_write_all(out_fd, (unsigned char *)line, (size_t)len) != 0) {
            return -1;
        }
    }
    return 0;
}

/* One line per block: "<offset>\t<mask as 16 hex digits>\n". */
static int emit_mask(int out_fd, uint64_t offset, uint64_t mask) {
    char line[40];
    int  len = snprintf(line, sizeof(line), "%" PRIu64 "\t%016" PRIx64 "\n", offset, mask);
    return jsp_write_all(out_fd, (unsigned char *)line, (size_t)len);
}

/* trace's inspection stream for one block, to trace_fd: independent of
   whatever mode is doing with the same block's mask on out_fd. */
static int emit_trace(int trace_fd, uint64_t offset, jsp_block block, uint64_t mask,
                      jsp_trace_mode trace) {
    switch (trace) {
    case JSP_TRACE_OFFSETS:
        return emit_offsets(trace_fd, offset, block, mask);
    case JSP_TRACE_MASKS:
        return emit_mask(trace_fd, offset, mask);
    case JSP_TRACE_NONE:
    default:
        return 0;
    }
}

/* Classifies one block, turns off structural recognition inside strings,
   traces the result to trace_fd, and hands it to mode's own output on
   out_fd, partial or complete block alike. A short block's mask is trimmed
   to its real bytes so the padding reports nothing; the assert is what
   makes that shift defined, since 1 << len would not be at len 64.
   string_state carries in_string and trailing_backslash_unpaired across calls, one call
   per block in stream order, padding included, since jsp_string_mask reads
   every byte of block.bytes regardless of len. */
static int process_block(int out_fd, uint64_t offset, jsp_block block, jsp_output_mode mode,
                         int trace_fd, jsp_trace_mode trace, jsp_string_state *string_state,
                         jsp_pluck_state *pluck_state) {
    assert(block.len >= 1 && block.len <= JSP_BLOCK);
    jsp_char_masks classified = jsp_classify_masks64(block.bytes);
    uint64_t       mask = jsp_filter_structural_mask(classified, string_state);
    if (block.len != JSP_BLOCK) {
        mask &= ~(uint64_t)0 >> (JSP_BLOCK - block.len);
    }
    if (emit_trace(trace_fd, offset, block, mask, trace) != 0) {
        return -1;
    }
    if (mode == JSP_OUTPUT_PLUCK) {
        return jsp_pluck_step(pluck_state, block, mask, out_fd);
    }
    return 0; /* JSP_OUTPUT_SINK: out_fd is never touched */
}

/* Closes whatever mode left in flight once the stream ends. Only
   JSP_OUTPUT_PLUCK needs this, for a bare scalar record cut off with no
   trailing whitespace or structural byte to end it; see jsp_pluck_finish. */
static int process_finish(int out_fd, jsp_output_mode mode, jsp_pluck_state *pluck_state) {
    if (mode == JSP_OUTPUT_PLUCK) {
        return jsp_pluck_finish(pluck_state, out_fd);
    }
    return 0;
}

typedef enum {
    JSP_READER_BLOCK, /* block holds the next block, full or the trailing partial one */
    JSP_READER_END,   /* the stream is exhausted; no block follows */
    JSP_READER_ERROR, /* read(2) failed; errno is set */
} jsp_reader_status;

/* block is meaningful only when status is JSP_READER_BLOCK. */
typedef struct {
    jsp_reader_status status;
    jsp_block         block;
} jsp_reader_result;

/* Turns a byte stream into a sequence of blocks. buf must hold cap + JSP_PAD
   bytes, cap a multiple of JSP_BLOCK: the pad is where a trailing partial
   block gets padded, past the real bytes read into it. */
typedef struct {
    int      fd;
    uint8_t *buf;
    size_t   cap;
    size_t   fill; /* valid bytes at buf[0..fill) */
    size_t   pos;  /* of those, already handed out as blocks; buf[pos..fill) remains */
    bool     done; /* the trailing partial block, if any, has already been returned */
} jsp_reader;

static void jsp_reader_init(jsp_reader *r, int fd, uint8_t *buf, size_t cap) {
    r->fd = fd;
    r->buf = buf;
    r->cap = cap;
    r->fill = 0;
    r->pos = 0;
    r->done = false;
}

/* Reports which of the three ways the stream answered, with the next block
   when it answered with one. Never yields a zero-length block: a stream
   ending on a block boundary goes straight to JSP_READER_END rather than one
   more, empty, block. */
static jsp_reader_result jsp_reader_next(jsp_reader *r) {
    if (r->done) {
        return (jsp_reader_result){.status = JSP_READER_END};
    }

    if (r->pos + JSP_BLOCK <= r->fill) {
        jsp_block block = jsp_block_make(r->buf + r->pos, JSP_BLOCK);
        r->pos += JSP_BLOCK;
        return (jsp_reader_result){.status = JSP_READER_BLOCK, .block = block};
    }

    size_t remainder = r->fill - r->pos;
    if (remainder > 0) {
        memmove(r->buf, r->buf + r->pos, remainder);
    }
    r->fill = remainder;
    r->pos = 0;

    for (;;) {
        ssize_t n = read(r->fd, r->buf + r->fill, r->cap - r->fill);
        /* clang-format off */
        if (n < 0 && errno == EINTR) { continue; }  /* interrupted, retry */
        if (n < 0)                  { return (jsp_reader_result){.status = JSP_READER_ERROR}; }
        /* clang-format on */
        if (n == 0) {
            r->done = true;
            if (r->fill == 0) {
                return (jsp_reader_result){.status = JSP_READER_END};
            }
            /* A space is not structural, so padding with it reports nothing. */
            memset(r->buf + r->fill, 0x20, JSP_BLOCK - r->fill);
            jsp_block block = jsp_block_make(r->buf, (unsigned)r->fill);
            r->pos = r->fill;
            return (jsp_reader_result){.status = JSP_READER_BLOCK, .block = block};
        }

        r->fill += (size_t)n;
        if (r->fill >= JSP_BLOCK) {
            jsp_block block = jsp_block_make(r->buf, JSP_BLOCK);
            r->pos = JSP_BLOCK;
            return (jsp_reader_result){.status = JSP_READER_BLOCK, .block = block};
        }
    }
}

static inline jsp_result jsp_result_make_(jsp_status status) {
    return (jsp_result){.status = status, .sys_errno = status == JSP_OK ? 0 : errno};
}

jsp_result jsp_run(jsp_fds fds, jsp_settings settings) {
    size_t   cap = round_up_block(settings.buf_size);
    uint8_t *buf = malloc(cap + JSP_PAD);
    if (buf == NULL) {
        return jsp_result_make_(JSP_ERR_ALLOC);
    }

    jsp_reader reader;
    jsp_reader_init(&reader, fds.in_fd, buf, cap);

    uint64_t   offset = 0;
    jsp_status result = JSP_OK;

    jsp_string_state string_state = {0};
    jsp_pluck_state  pluck_state = {0};

    for (;;) {
        jsp_reader_result next = jsp_reader_next(&reader);
        /* clang-format off */
        if (next.status == JSP_READER_END)      { break; }
        if (next.status == JSP_READER_ERROR)    { result = JSP_ERR_IO; break; }
        /* clang-format on */
        if (process_block(fds.out_fd, offset, next.block, settings.output, fds.trace_fd,
                          settings.trace, &string_state, &pluck_state) != 0) {
            result = JSP_ERR_BLOCK_PROCESS_TMP;
            break;
        }
        offset += JSP_BLOCK;
    }

    if (result == JSP_OK) {
        result = process_finish(fds.out_fd, settings.output, &pluck_state) == 0
                     ? JSP_OK
                     : JSP_ERR_BLOCK_PROCESS_TMP;
    }

    free(buf);
    return jsp_result_make_(result);
}
