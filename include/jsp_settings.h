#ifndef JSP_SETTINGS_H
#define JSP_SETTINGS_H

#include "jsp.h"
#include "jstr.h"

#include <stddef.h>

/* Everything the app can be configured with, cli-supplied or defaulted
   alike. path is meaningful only when output is JSP_OUTPUT_PLUCK; it views
   the matching argv entry, which outlives the process. */
typedef struct {
    size_t          buf_size;
    jsp_output_mode output;
    jstr            path;
} jsp_settings;

/* Parses argv[1..argc) into a jsp_settings, filling in defaults for anything
   not given on the command line. Prints a message to stderr and exits with
   status 1 on an invalid or unrecognized argument. */
jsp_settings jsp_settings_parse(int argc, char **argv);

#endif /* JSP_SETTINGS_H */
