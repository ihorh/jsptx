#include "jsp.h"
#include "jsp_settings.h"

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    jsp_settings settings = jsp_settings_parse(argc, argv);

    errno = 0;
    if (jsp_run(0, 1, settings.buf_size, settings.output, STDERR_FILENO, settings.trace) != 0) {
        if (errno != 0) {
            perror("jsptx");
        } else {
            fprintf(stderr, "jsptx: malformed input\n");
        }
        return 1;
    }
    return 0;
}
