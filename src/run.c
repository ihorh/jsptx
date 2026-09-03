#define _POSIX_C_SOURCE 200809L

#include "jsp.h"

#include <errno.h>
#include <stdlib.h>
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

int jsp_run(int in_fd, int out_fd, size_t buf_size) {
    size_t         cap = round_up_block(buf_size);
    unsigned char *buf = malloc(cap + JSP_PAD);
    if (buf == NULL) {
        return -1;
    }

    int result = 0;
    for (;;) {
        ssize_t n = read(in_fd, buf, cap);
        /* clang-format off */
        if (n == 0)                  { break; }                 /* end of file */
        if (n < 0 && errno == EINTR) { continue; }              /* interrupted, retry */
        if (n < 0)                   { result = -1; break; }    /* real read error */
        /* clang-format on */
        if (write_all(out_fd, buf, (size_t)n) != 0) {
            result = -1;
            break;
        }
    }

    free(buf);
    return result;
}
