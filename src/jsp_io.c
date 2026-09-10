#define _POSIX_C_SOURCE 200809L

#include "jsp_io.h"

#include <errno.h>
#include <unistd.h>

int jsp_write_all(int fd, const unsigned char *buf, size_t len) {
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
