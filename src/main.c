#include "jsp.h"
#include "jsp_settings.h"

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    jsp_fds fds = {
        .in_fd = STDIN_FILENO,
        .out_fd = STDOUT_FILENO,
        .trace_fd = STDERR_FILENO,
        .err_fd = STDERR_FILENO,
    };
    jsp_settings settings = jsp_settings_parse(argc, argv);

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
