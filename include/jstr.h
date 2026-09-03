/*
 * jstr.h
 * A non-owning string view. Trimmed from jcraft's libs/jstring down to what
 * jsptx's flag parsing uses.
 *
 * Copyright (c) 2026 Ihor H.
 * SPDX-License-Identifier: MIT
 */
#ifndef JSTR_H
#define JSTR_H

#include <assert.h>
#include <stddef.h>
#include <string.h>

/* A view over bytes that live somewhere else, borrowed and never owned.
   len is where it ends — not a NUL byte. */
typedef struct jstr {
    const char *data;
    ptrdiff_t   len;
} jstr;

/* A jstr from a string literal, with no runtime strlen. The `""` rejects a
   non-literal at compile time instead of silently strlen-ing the wrong thing. */
#define JSTR(lit) ((jstr){"" lit, (ptrdiff_t)sizeof(lit) - 1})

/* A jstr over a long-lived C string (argv...) — one strlen, not one per use.
   NULL-safe: a NULL z yields an empty jstr. */
static inline jstr jstr_init(const char *z) { return (jstr){z, z ? (ptrdiff_t)strlen(z) : 0}; }

/* Empty is a len at or below zero, or a NULL data whatever len says. */
static inline _Bool jstr_empty(jstr s) { return s.data == NULL || s.len <= 0; }

/* The view past s's first n bytes: what is left to read, once n bytes are read.
   n == s.len gives an empty jstr, the normal end of such a walk rather than
   an error. */
static inline jstr jstr_after(jstr s, ptrdiff_t n) {
    assert(n >= 0 && n <= s.len);
    return (jstr){s.data + n, s.len - n};
}

/* Whether s's first bytes are prefix, byte for byte. A prefix longer than s
   never matches. Not in upstream jstring; added here because jsptx's flag
   parsing needs it. */
static inline _Bool jstr_starts_with(jstr s, jstr prefix) {
    return s.len >= prefix.len && memcmp(s.data, prefix.data, (size_t)prefix.len) == 0;
}

#endif /* JSTR_H */
