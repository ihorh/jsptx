#include "jsp_settings.h"

#include "jstr.h"

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define JSP_DEFAULT_BUF_SIZE ((size_t)65536)

static const char *usage_text =
    "Usage: jsptx [PATH] [OPTIONS]\n"
    "\n"
    "  Reads a JSON stream on stdin. With PATH, prints each record's matching\n"
    "  value verbatim, all inside one JSON array. PATH is \".\" for the whole\n"
    "  record, or \".key.key\" for a nested value. Without PATH, classifies the\n"
    "  stream and discards the result.\n"
    "\n"
    "Options:\n"
    "  --lines        print one value per line instead of one JSON array\n"
    "  --buf-size=N   read in chunks of N bytes (default %zu)\n"
    "  --offsets      trace every structural character's offset to stderr\n"
    "  --masks        trace each block's classification to stderr, as hex\n"
    "  -h, --help     print this message and exit\n";

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

static jsp_path parse_path(jstr arg) {
    jsp_path path;
    if (!jsp_path_parse(arg, &path)) {
        fprintf(stderr,
                "jsptx: invalid path: %.*s (want \".\" or \".key.key\", at most %d keys)\n",
                (int)arg.len, arg.data, JSP_PATH_MAX_SEGMENTS);
        exit(1);
    }
    return path;
}

static const jstr FLG_HELP = JSTR("--help");
static const jstr FLG_HELP_S = JSTR("-h");
static const jstr FLG_OFFSET = JSTR("--offsets");
static const jstr FLG_MASKS = JSTR("--masks");
static const jstr FLG_LINES = JSTR("--lines");
static const jstr OPT_BUF_SIZE = JSTR("--buf-size=");

static const jsp_settings JSP_SETTINGS_DEFAULT = {
    .buf_size = JSP_DEFAULT_BUF_SIZE,
    .output = JSP_OUTPUT_SINK,
    .trace = JSP_TRACE_NONE,
};

/* Plucked records inside one JSON array, the default. */
static const jsp_framing FRAMING_ARRAY = {
    .open = JSTR("["),
    .separator = JSTR(","),
    .close = JSTR("]\n"),
};

/* Plucked records one per line, under --lines. */
static const jsp_framing FRAMING_LINES = {
    .separator = JSTR("\n"),
    .close = JSTR("\n"),
};

jsp_settings jsp_settings_parse(int argc, char **argv) {
    jsp_settings settings = JSP_SETTINGS_DEFAULT;
    bool         have_path = false;
    bool         lines = false;

    for (int i = 1; i < argc; i++) {
        jstr arg = jstr_init(argv[i]);
        if (jstr_equal(arg, FLG_HELP_S) || jstr_equal(arg, FLG_HELP)) {
            fprintf(stdout, usage_text, JSP_DEFAULT_BUF_SIZE);
            exit(0);
        } else if (jstr_starts_with(arg, OPT_BUF_SIZE)) {
            settings.buf_size = parse_buf_size(jstr_after(arg, OPT_BUF_SIZE.len));
        } else if (jstr_equal(arg, FLG_OFFSET)) {
            settings.trace = JSP_TRACE_OFFSETS;
        } else if (jstr_equal(arg, FLG_MASKS)) {
            settings.trace = JSP_TRACE_MASKS;
        } else if (jstr_equal(arg, FLG_LINES)) {
            lines = true;
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

    if (have_path) {
        settings.framing = lines ? FRAMING_LINES : FRAMING_ARRAY;
    }
    return settings;
}
