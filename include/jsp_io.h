#ifndef JSP_IO_H
#define JSP_IO_H

#include "jstr.h"

#include <stddef.h>

/* Writes exactly len bytes from buf to fd, resuming on a short write or an
   EINTR the way write(2) can return either on a pipe. Returns 0, or -1 on a
   real write error with errno set by the failing call. */
int jsp_write_all(int fd, const unsigned char *buf, size_t len);

/* Writes all of s to fd, the same way jsp_write_all does. */
static inline int jsp_write_jstr(int fd, jstr s) {
    return jsp_write_all(fd, (const unsigned char *)s.data, (size_t)s.len);
}

#endif /* JSP_IO_H */
