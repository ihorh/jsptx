#include "jsp_run.h"

#include "jsp_classify.h"
#include "jsp_io.h"
#include "jsp_pluck.h"
#include "jsp_reader.h"
#include "jsp_string_mask.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* One classification covers JSP_BLOCK bytes. */
#define JSP_BLOCK 64

static size_t round_up_block(size_t n) {
    if (n < JSP_BLOCK) {
        return JSP_BLOCK;
    }
    return (n + JSP_BLOCK - 1) / JSP_BLOCK * JSP_BLOCK;
}

/* One line per set bit in mask: "<offset + bit>\t<block.ptr[bit]>\n". */
static int emit_offsets(int out_fd, uint64_t offset, jsp_slice block, uint64_t mask) {
    while (mask != 0) {
        int bit = __builtin_ctzll(mask);
        mask &= mask - 1;

        char line[32];
        int  len = snprintf(line, sizeof(line), "%" PRIu64 "\t%c\n", offset + (uint64_t)bit,
                            block.ptr[bit]);
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
static int emit_trace(int trace_fd, uint64_t offset, jsp_slice block, uint64_t mask,
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
   every byte of block.ptr regardless of len. */
static int process_block(int out_fd, uint64_t offset, jsp_slice block, jsp_output_mode mode,
                         int trace_fd, jsp_trace_mode trace, jsp_string_state *string_state,
                         jsp_pluck_state *pluck_state) {
    assert(block.len >= 1 && block.len <= JSP_BLOCK);
    jsp_char_masks classified = jsp_classify_masks64(block.ptr);
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

static inline jsp_result jsp_result_make_(jsp_status status) {
    return (jsp_result){.status = status, .sys_errno = status == JSP_OK ? 0 : errno};
}

jsp_result jsp_run(jsp_fds fds, jsp_settings settings) {
    size_t   cap = round_up_block(settings.buf_size);
    uint8_t *buf = malloc(cap);
    if (buf == NULL) {
        return jsp_result_make_(JSP_ERR_ALLOC);
    }

    /* A space is not structural, so a padded tail reports nothing. */
    jsp_reader reader;
    jsp_reader_init(&reader, fds.in_fd, jsp_buf_make(buf, cap), JSP_BLOCK, 0x20);

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
