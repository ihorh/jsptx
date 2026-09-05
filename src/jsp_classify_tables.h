#ifndef JSP_CLASSIFY_TABLES_H
#define JSP_CLASSIFY_TABLES_H

/* jsp_classify_tables — the nibble-lookup tables classify_neon.c and
   classify_avx2.c both use to tag a byte with which of the eight special
   JSON characters it is, if any.

   The trick behind them: split a byte into its low and high nibble, look
   each nibble up in its own 16-entry table, then AND the two results. For
   any of the eight characters this recovers exactly that character's own
   bit; for anything else it recovers 0. One vqtbl1q_u8/_mm256_shuffle_epi8
   per table does 16 (NEON) or 32 (AVX2) of these lookups at once, rather
   than one compare per character.

   Shared between the two backends because the table contents are identical
   — only how each backend loads and applies them differs. */

#include <stdint.h>

/* One bit per character. classify_tag16 and classify_tag32 use these to mark
   which of the eight characters a byte is, if any. */
enum {
    JSP_BIT_LBRACE = 1 << 0,    /* '{' 0x7B */
    JSP_BIT_RBRACE = 1 << 1,    /* '}' 0x7D */
    JSP_BIT_LBRACKET = 1 << 2,  /* '[' 0x5B */
    JSP_BIT_RBRACKET = 1 << 3,  /* ']' 0x5D */
    JSP_BIT_COLON = 1 << 4,     /* ':' 0x3A */
    JSP_BIT_COMMA = 1 << 5,     /* ',' 0x2C */
    JSP_BIT_QUOTE = 1 << 6,     /* '"' 0x22 */
    JSP_BIT_BACKSLASH = 1 << 7, /* '\' 0x5C */
};

/* Every structural bit at once - every bit above except `JSP_BIT_BACKSLASH`:
   '\' is tracked for string-escape handling, not as a structural character. */
#define JSP_STRUCTURAL_BITS 0x7F

/* Indexed by a byte's low nibble: JSP_LOW_NIBBLE_TABLE[c & 0xF] holds every
   character's bit whose low nibble is that value, OR'd together. Plain
   uint8_t[16] rather than a vector type, so it can be sparse (unlisted
   entries are 0) and so AVX2 can load it with _mm_loadu_si128 and broadcast
   it into both 128-bit halves of its 32-byte table register; NEON loads the
   same bytes with vld1q_u8. */
static const uint8_t JSP_LOW_NIBBLE_TABLE[16] = {
    [0x2] = JSP_BIT_QUOTE,                     /* '"' */
    [0xA] = JSP_BIT_COLON,                     /* ':' */
    [0xB] = JSP_BIT_LBRACE | JSP_BIT_LBRACKET, /* '{' '[' */
    [0xC] = JSP_BIT_COMMA | JSP_BIT_BACKSLASH, /* ',' '\' */
    [0xD] = JSP_BIT_RBRACE | JSP_BIT_RBRACKET, /* '}' ']' */
};

/* Indexed by a byte's high nibble, the other half of the same lookup:
   JSP_HIGH_NIBBLE_TABLE[c >> 4] holds every character's bit whose high
   nibble is that value, OR'd together. */
static const uint8_t JSP_HIGH_NIBBLE_TABLE[16] = {
    [0x2] = JSP_BIT_COMMA | JSP_BIT_QUOTE,                           /* ',' '"' */
    [0x3] = JSP_BIT_COLON,                                           /* ':' */
    [0x5] = JSP_BIT_LBRACKET | JSP_BIT_RBRACKET | JSP_BIT_BACKSLASH, /* '[' ']' '\' */
    [0x7] = JSP_BIT_LBRACE | JSP_BIT_RBRACE,                         /* '{' '}' */
};

#endif /* JSP_CLASSIFY_TABLES_H */
