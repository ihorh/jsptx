#define _POSIX_C_SOURCE 200809L

#include "jsp_reader.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

void jsp_reader_init(jsp_reader *r, int fd, jsp_buf buf, size_t block_size, uint8_t filler) {
    r->fd = fd;
    r->buf = buf;
    r->block_size = block_size;
    r->filler = filler;
    r->pos = 0;
    r->done = false;
}

jsp_reader_result jsp_reader_next(jsp_reader *r) {
    if (r->done) {
        return (jsp_reader_result){.status = JSP_READER_END};
    }

    if (r->pos + r->block_size <= r->buf.len) {
        jsp_slice block = jsp_slice_make(r->buf.ptr + r->pos, r->block_size);
        r->pos += r->block_size;
        return (jsp_reader_result){.status = JSP_READER_BLOCK, .block = block};
    }

    jsp_buf_compact(&r->buf, r->pos);
    r->pos = 0;

    for (;;) {
        ssize_t n = read(r->fd, jsp_buf_free(r->buf), jsp_buf_room(r->buf));
        /* clang-format off */
        if (n < 0 && errno == EINTR) { continue; }  /* interrupted, retry */
        if (n < 0)                  { return (jsp_reader_result){.status = JSP_READER_ERROR}; }
        /* clang-format on */
        if (n == 0) {
            r->done = true;
            if (r->buf.len == 0) {
                return (jsp_reader_result){.status = JSP_READER_END};
            }
            memset(jsp_buf_free(r->buf), r->filler, r->block_size - r->buf.len);
            jsp_slice block = jsp_slice_make(r->buf.ptr, r->buf.len);
            r->pos = r->buf.len;
            return (jsp_reader_result){.status = JSP_READER_BLOCK, .block = block};
        }

        r->buf.len += (size_t)n;
        if (r->buf.len >= r->block_size) {
            jsp_slice block = jsp_slice_make(r->buf.ptr, r->block_size);
            r->pos = r->block_size;
            return (jsp_reader_result){.status = JSP_READER_BLOCK, .block = block};
        }
    }
}
