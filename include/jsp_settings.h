#ifndef JSP_SETTINGS_H
#define JSP_SETTINGS_H

#include <stddef.h>

/* Everything the app can be configured with, cli-supplied or defaulted alike.
   One field for now: buf_size. */
typedef struct {
    size_t buf_size;
} jsp_settings;

/* Parses argv[1..argc) into a jsp_settings, filling in defaults for anything
   not given on the command line. Prints a message to stderr and exits with
   status 1 on an invalid or unrecognized argument. */
jsp_settings jsp_settings_parse(int argc, char **argv);

#endif /* JSP_SETTINGS_H */
