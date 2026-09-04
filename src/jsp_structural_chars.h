#ifndef JSP_STRUCTURAL_CHARS_H
#define JSP_STRUCTURAL_CHARS_H

#include <stdint.h>

/* The seven structural JSON characters classify64 looks for, listed once so
   every implementation drives its comparisons from the same set. Pass a
   function-like macro X(ch); it is invoked once per character, so
   JSP_STRUCTURAL_CHARS(X) expands to X('{') X('}') X('[') X(']') X(':')
   X(',') X('"'). */
#define JSP_STRUCTURAL_CHARS(X)                                                                \
    X('{')                                                                                     \
    X('}')                                                                                     \
    X('[')                                                                                     \
    X(']')                                                                                     \
    X(':')                                                                                     \
    X(',')                                                                                     \
    X('"')

static inline int jsp_char_is_structural(uint8_t c) {
#define JSP_IS(ch) || c == (ch)
    return 0 JSP_STRUCTURAL_CHARS(JSP_IS);
#undef JSP_IS
}

#endif /* JSP_STRUCTURAL_CHARS_H */
