#ifndef JSP_IO_H
#define JSP_IO_H

#include <stddef.h>

/* Writes exactly len bytes from buf to fd, resuming on a short write or an
   EINTR the way write(2) can return either on a pipe. Returns 0, or -1 on a
   real write error with errno set by the failing call. */
int jsp_write_all(int fd, const unsigned char *buf, size_t len);

#endif /* JSP_IO_H */
