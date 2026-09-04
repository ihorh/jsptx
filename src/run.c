#define _POSIX_C_SOURCE 200809L

#include "jsp.h"

#include "jsp_classify.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define JSP_BLOCK 64
#define JSP_PAD 64

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

/* One line per set bit in mask: "<offset + bit>\t<block[bit]>\n". */
static int
emit_offsets(int out_fd, uint64_t offset, const unsigned char *block, uint64_t mask) {
    while (mask != 0) {
        int bit = __builtin_ctzll(mask);
        mask &= mask - 1;

        char line[32];
        int  len = snprintf(
            line, sizeof(line), "%" PRIu64 "\t%c\n", offset + (uint64_t)bit, block[bit]
        );
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

static int
emit_block(int out_fd, uint64_t offset, const unsigned char *block, uint64_t mask, bool masks) {
    return masks ? emit_mask(out_fd, offset, mask) : emit_offsets(out_fd, offset, block, mask);
}

int jsp_run(int in_fd, int out_fd, size_t buf_size, bool masks) {
    size_t         cap = round_up_block(buf_size);
    unsigned char *buf = malloc(cap + JSP_PAD);
    if (buf == NULL) {
        return -1;
    }

    size_t   pending = 0; /* bytes at buf[0..pending), carried over; always < JSP_BLOCK */
    uint64_t blocks_consumed = 0;
    int      result = 0;

    for (;;) {
        ssize_t n = read(in_fd, buf + pending, cap - pending);
        /* clang-format off */
        if (n < 0 && errno == EINTR) { continue; }              /* interrupted, retry */
        if (n < 0)                   { result = -1; break; }    /* real read error */
        /* clang-format on */

        if (n == 0) {
            /* End of file: pending bytes are the final, possibly partial,
               block. Pad it with spaces, classify, then drop mask bits past
               the real length so the padding reports nothing. */
            if (pending > 0) {
                memset(buf + pending, 0x20, JSP_BLOCK - pending);
                uint64_t real_bytes_mask = ((uint64_t)1 << pending) - 1;
                uint64_t mask = jsp_classify64(buf) & real_bytes_mask;
                if (emit_block(out_fd, blocks_consumed * JSP_BLOCK, buf, mask, masks) != 0) {
                    result = -1;
                }
            }
            break;
        }

        size_t total = pending + (size_t)n;
        size_t nblocks = total / JSP_BLOCK;
        for (size_t b = 0; b < nblocks; b++) {
            unsigned char *block = buf + b * JSP_BLOCK;
            uint64_t       mask = jsp_classify64(block);
            uint64_t       offset = (blocks_consumed + b) * JSP_BLOCK;
            if (emit_block(out_fd, offset, block, mask, masks) != 0) {
                result = -1;
                break;
            }
        }
        if (result != 0) {
            break;
        }

        size_t consumed = nblocks * JSP_BLOCK;
        pending = total - consumed;
        if (pending > 0) {
            memmove(buf, buf + consumed, pending);
        }
        blocks_consumed += nblocks;
    }

    free(buf);
    return result;
}
