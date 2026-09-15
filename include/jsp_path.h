#ifndef JSP_PATH_H
#define JSP_PATH_H

#include "jstr.h"

#include <stddef.h>

/* A path segment names a key one level deeper, and nesting stops at 64
   levels, so a path with more segments than that could never match. */
#define JSP_PATH_MAX_SEGMENTS 64

/* A parsed path: its text, and where every segment sits in it, found once so
   that neither the count nor a segment needs a scan. Views text, which must
   outlive it. */
typedef struct {
    jstr      text;                            /* "." or ".seg.seg" */
    unsigned  segments;                        /* 0 for "." */
    ptrdiff_t dots[JSP_PATH_MAX_SEGMENTS + 1]; /* the '.' opening each segment, then text.len */
} jsp_path;

/* Parses text into out: "." alone, or one or more ".segment" pieces, where a
   segment is any bytes but '.', never empty, and there are at most
   JSP_PATH_MAX_SEGMENTS of them. Returns false when text breaks that
   grammar, leaving out unspecified. */
static inline _Bool jsp_path_parse(jstr text, jsp_path *out) {
    if (text.len < 1 || text.data[0] != '.') {
        return 0;
    }
    out->text = text;
    out->segments = 0;
    if (text.len > 1) {
        for (ptrdiff_t i = 0; i < text.len; i++) {
            if (text.data[i] != '.') {
                continue;
            }
            if (i + 1 == text.len || text.data[i + 1] == '.' ||
                out->segments == JSP_PATH_MAX_SEGMENTS) {
                return 0;
            }
            out->dots[out->segments++] = i;
        }
    }
    out->dots[out->segments] = text.len;
    return 1;
}

/* Segment i of p, counting from 0. The caller keeps i below p->segments. */
static inline jstr jsp_path_segment(const jsp_path *p, unsigned i) {
    ptrdiff_t start = p->dots[i] + 1;
    return (jstr){p->text.data + start, p->dots[i + 1] - start};
}

#endif /* JSP_PATH_H */
