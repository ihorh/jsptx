#include "jsp_settings.h"

#include "jstr.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#define JSP_DEFAULT_BUF_SIZE ((size_t)65536)

typedef struct {
    int  value;
    jstr rest;
} jsp_uint_parse;

/* The value of the leading digit run of s, and s past it. Overflow saturates
   to INT_MAX rather than wrapping. An s with no leading digit parses as value
   0 with rest equal to s. */
static jsp_uint_parse parse_uint(jstr s) {
    int       v = 0;
    ptrdiff_t i = 0;
    for (; i < s.len; i++) {
        unsigned char c = (unsigned char)s.data[i];
        if (c < '0' || c > '9') {
            break;
        }
        int d = c - '0';
        v = v <= (INT_MAX - d) / 10 ? v * 10 + d : INT_MAX;
    }
    return (jsp_uint_parse){v, jstr_after(s, i)};
}

static size_t parse_buf_size(jstr value) {
    jsp_uint_parse p = parse_uint(value);
    if (p.value <= 0 || !jstr_empty(p.rest)) {
        fprintf(stderr, "jsptx: invalid --buf-size value: %.*s\n", (int)value.len, value.data);
        exit(1);
    }
    return (size_t)p.value;
}

jsp_settings jsp_settings_parse(int argc, char **argv) {
    jsp_settings settings = {.buf_size = JSP_DEFAULT_BUF_SIZE, .output = JSP_OUTPUT_OFFSETS};
    jstr         prefix = JSTR("--buf-size=");
    jstr         masks_flag = JSTR("--masks");
    jstr         sink_flag = JSTR("--sink");

    for (int i = 1; i < argc; i++) {
        jstr arg = jstr_init(argv[i]);
        if (jstr_starts_with(arg, prefix)) {
            settings.buf_size = parse_buf_size(jstr_after(arg, prefix.len));
        } else if (jstr_equal(arg, masks_flag)) {
            settings.output = JSP_OUTPUT_MASKS;
        } else if (jstr_equal(arg, sink_flag)) {
            settings.output = JSP_OUTPUT_SINK;
        } else {
            fprintf(stderr, "jsptx: unrecognized argument: %s\n", argv[i]);
            exit(1);
        }
    }

    return settings;
}
