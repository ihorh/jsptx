#define _POSIX_C_SOURCE 200809L

#include "jsp.h"

#include "jsp_block.h"
#include "jsp_classify.h"
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

/* write(2) on a pipe returns short, same as read(2), so this resumes until
   every byte is written or a real error stops it. */
static int write_all(int fd, const unsigned char *buf, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, buf + written, len - written);
        /* clang-format off */
        if (n < 0 && errno == EINTR) { continue; }  /* interrupted, retry */
        if (n < 0)                   { return -1; } /* real write error */
        /* clang-format on */
        written += (size_t)n;
    }
    return 0;
}

/* One line per set bit in mask: "<offset + bit>\t<block.bytes[bit]>\n". */
static int emit_offsets(int out_fd, uint64_t offset, jsp_block block, uint64_t mask) {
    while (mask != 0) {
        int bit = __builtin_ctzll(mask);
        mask &= mask - 1;

        char line[32];
        int  len = snprintf(line, sizeof(line), "%" PRIu64 "\t%c\n", offset + (uint64_t)bit,
                            block.bytes[bit]);
        if (write_all(out_fd, (unsigned char *)line, (size_t)len) != 0) {
            return -1;
        }
    }
    return 0;
}

/* One line per block: "<offset>\t<mask as 16 hex digits>\n". */
static int emit_mask(int out_fd, uint64_t offset, uint64_t mask) {
    char line[40];
    int  len = snprintf(line, sizeof(line), "%" PRIu64 "\t%016" PRIx64 "\n", offset, mask);
    return write_all(out_fd, (unsigned char *)line, (size_t)len);
}

/* Sink writes nothing: the point is measuring classification and string
   masking apart from the cost of formatting and writing a result. */
static int
emit_block(int out_fd, uint64_t offset, jsp_block block, uint64_t mask, jsp_output_mode mode) {
    switch (mode) {
    case JSP_OUTPUT_MASKS:
        return emit_mask(out_fd, offset, mask);
    case JSP_OUTPUT_SINK:
        return 0;
    case JSP_OUTPUT_OFFSETS:
    default:
        return emit_offsets(out_fd, offset, block, mask);
    }
}

/* Classifies one block, turns off structural recognition inside strings, and
   emits the result, partial or complete alike. A short block's mask is
   trimmed to its real bytes so the padding reports nothing; the assert is
   what makes that shift defined, since 1 << len would not be at len 64.
   string_state carries in_string and in_backslash_run across calls, one call
   per block in stream order, padding included, since jsp_string_mask reads
   every byte of block.bytes regardless of len. */
static int process_block(int out_fd, uint64_t offset, jsp_block block, jsp_output_mode mode,
                         jsp_string_state *string_state) {
    assert(block.len >= 1 && block.len <= JSP_BLOCK);
    jsp_char_masks classified = jsp_classify_masks64(block.bytes);
    uint64_t       mask = jsp_string_mask(classified.structural, classified.quote,
                                          classified.backslash, string_state);
    if (block.len != JSP_BLOCK) {
        mask &= ~(uint64_t)0 >> (JSP_BLOCK - block.len);
    }
    return emit_block(out_fd, offset, block, mask, mode);
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

int jsp_run(int in_fd, int out_fd, size_t buf_size, jsp_output_mode mode) {
    size_t   cap = round_up_block(buf_size);
    uint8_t *buf = malloc(cap + JSP_PAD);
    if (buf == NULL) {
        return -1;
    }

    jsp_reader reader;
    jsp_reader_init(&reader, in_fd, buf, cap);

    uint64_t offset = 0;
    int      result = 0;

    jsp_string_state string_state = {0};

    for (;;) {
        jsp_reader_result next = jsp_reader_next(&reader);
        /* clang-format off */
        if (next.status == JSP_READER_END)      { break; }
        if (next.status == JSP_READER_ERROR)    { result = -1; break; }
        /* clang-format on */
        if (process_block(out_fd, offset, next.block, mode, &string_state) != 0) {
            result = -1;
            break;
        }
        offset += JSP_BLOCK;
    }

    free(buf);
    return result;
}
