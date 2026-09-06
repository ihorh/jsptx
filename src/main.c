#include "jsp.h"
#include "jsp_settings.h"

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    jsp_settings settings = jsp_settings_parse(argc, argv);

    /* Trace defaults alongside jsptx's own diagnostics, but is its own fd
       rather than incidentally the same literal. */
    jsp_fds fds = {.in_fd = 0, .out_fd = 1, .trace_fd = STDERR_FILENO, .err_fd = STDERR_FILENO};

    errno = 0;
    if (jsp_run(fds, settings) != 0) {
        if (errno != 0) {
            perror("jsptx");
        } else {
            fprintf(stderr, "jsptx: malformed input\n");
        }
        return 1;
    }
    return 0;
}
