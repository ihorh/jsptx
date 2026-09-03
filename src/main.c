#include "jsp.h"
#include "jsp_settings.h"

#include <stdio.h>

int main(int argc, char **argv) {
    jsp_settings settings = jsp_settings_parse(argc, argv);

    if (jsp_run(0, 1, settings.buf_size) != 0) {
        perror("jsptx");
        return 1;
    }
    return 0;
}
