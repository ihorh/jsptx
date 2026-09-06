#include "jsp.h"
#include "jsp_settings.h"

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    jsp_settings settings = jsp_settings_parse(argc, argv);

    /* jsptx's own diagnostics (perror, argument errors) always go to
       stderr; trace defaults there too, alongside them, but is its own
       named fd rather than a second literal STDERR_FILENO. */
    int err_fd = STDERR_FILENO;
    int trace_fd = err_fd;

    jsp_fds         fds = {.in_fd = 0, .out_fd = 1, .trace_fd = trace_fd};
    jsp_run_options options = {
        .buf_size = settings.buf_size, .mode = settings.output, .trace = settings.trace};

    errno = 0;
    if (jsp_run(fds, options) != 0) {
        if (errno != 0) {
            perror("jsptx");
        } else {
            fprintf(stderr, "jsptx: malformed input\n");
        }
        return 1;
    }
    return 0;
}
