#include "jsp_settings.h"

#include "jstr.h"

#include <limits.h>
#include <stdbool.h>
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

/* The path grammar this stage understands: "." alone, standing for the
   whole record. Segment splitting lands in pluck-path; until then, any
   other path is a clear error rather than a silent partial match. */
static jstr parse_path(jstr arg) {
    if (!jstr_equal(arg, JSTR("."))) {
        fprintf(stderr, "jsptx: unsupported path: %.*s (only \".\" is implemented so far)\n",
                (int)arg.len, arg.data);
        exit(1);
    }
    return arg;
}

static void print_usage(FILE *out) {
    fprintf(out,
            "Usage: jsptx [PATH] [OPTIONS]\n"
            "\n"
            "  Reads a JSON stream on stdin. With PATH, prints each record's matching\n"
            "  value verbatim, one per line; \".\" matches the whole record and is the\n"
            "  only path implemented so far. Without PATH, classifies the stream and\n"
            "  discards the result.\n"
            "\n"
            "Options:\n"
            "  --buf-size=N   read in chunks of N bytes (default %zu)\n"
            "  --offsets      trace every structural character's offset to stderr\n"
            "  --masks        trace each block's classification to stderr, as hex\n"
            "  -h, --help     print this message and exit\n",
            JSP_DEFAULT_BUF_SIZE);
}

jsp_settings jsp_settings_parse(int argc, char **argv) {
    jsp_settings settings = {
        .buf_size = JSP_DEFAULT_BUF_SIZE, .output = JSP_OUTPUT_SINK, .trace = JSP_TRACE_NONE};
    jstr prefix = JSTR("--buf-size=");
    jstr offsets_flag = JSTR("--offsets");
    jstr masks_flag = JSTR("--masks");
    bool have_path = false;

    for (int i = 1; i < argc; i++) {
        jstr arg = jstr_init(argv[i]);
        if (jstr_equal(arg, JSTR("-h")) || jstr_equal(arg, JSTR("--help"))) {
            print_usage(stdout);
            exit(0);
        } else if (jstr_starts_with(arg, prefix)) {
            settings.buf_size = parse_buf_size(jstr_after(arg, prefix.len));
        } else if (jstr_equal(arg, offsets_flag)) {
            settings.trace = JSP_TRACE_OFFSETS;
        } else if (jstr_equal(arg, masks_flag)) {
            settings.trace = JSP_TRACE_MASKS;
        } else if (jstr_starts_with(arg, JSTR("--"))) {
            fprintf(stderr, "jsptx: unrecognized argument: %s\n", argv[i]);
            exit(1);
        } else if (have_path) {
            fprintf(stderr, "jsptx: only one path is supported, got a second: %s\n", argv[i]);
            exit(1);
        } else {
            settings.path = parse_path(arg);
            settings.output = JSP_OUTPUT_PLUCK;
            have_path = true;
        }
    }

    return settings;
}
