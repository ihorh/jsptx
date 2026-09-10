#define _POSIX_C_SOURCE 200809L

#include "jsp_reader.h"

#include <errno.h>
#include <unistd.h>

void jsp_reader_init(jsp_reader *r, int fd, jsp_buf_u8 buf, size_t block_size) {
    r->fd = fd;
    r->buf = buf;
    r->block_size = block_size;
    r->buf_offset = 0;
    r->stream_offset = 0;
    r->done = false;
}

/* Hands out one block and moves both offsets past it. */
static jsp_reader_result yield_(jsp_reader *r, jsp_slice_u8 block) {
    uint64_t offset = r->stream_offset;
    r->buf_offset += block.len;
    r->stream_offset += block.len;
    return (jsp_reader_result){.status = JSP_READER_BLOCK, .block = block, .offset = offset};
}

jsp_reader_result jsp_reader_next(jsp_reader *r) {
    if (r->done) {
        return (jsp_reader_result){.status = JSP_READER_END};
    }

    if (r->buf_offset + r->block_size <= r->buf.len) {
        return yield_(r, jsp_slice_u8_make(r->buf.ptr + r->buf_offset, r->block_size));
    }

    jsp_buf_u8_compact(&r->buf, r->buf_offset);
    r->buf_offset = 0;

    for (;;) {
        jsp_buf_u8 tail = jsp_buf_u8_tail(r->buf);
        ssize_t    n = read(r->fd, tail.ptr, tail.cap);
        /* clang-format off */
        if (n < 0 && errno == EINTR) { continue; }  /* interrupted, retry */
        if (n < 0)                  { return (jsp_reader_result){.status = JSP_READER_ERROR}; }
        /* clang-format on */
        if (n == 0) {
            r->done = true;
            if (r->buf.len == 0) {
                return (jsp_reader_result){.status = JSP_READER_END};
            }
            return yield_(r, jsp_slice_u8_make(r->buf.ptr, r->buf.len));
        }

        r->buf.len += (size_t)n;
        if (r->buf.len >= r->block_size) {
            return yield_(r, jsp_slice_u8_make(r->buf.ptr, r->block_size));
        }
    }
}
