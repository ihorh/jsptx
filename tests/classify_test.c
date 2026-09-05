#include "jsp_classify.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Every position in a 64-byte block, holding one of the eight characters
   jsp_classify_masks64 knows about, must classify to exactly that bit in
   the right one of structural/quote/backslash, and nowhere else. */
static void check_masks_directed(void) {
    static const char structural_only[] = "{}[]:,";
    uint8_t           buf[64];

    for (size_t k = 0; k < sizeof(structural_only) - 1; k++) {
        for (size_t pos = 0; pos < 64; pos++) {
            memset(buf, 'x', sizeof(buf));
            buf[pos] = (uint8_t)structural_only[k];

            uint64_t       want = (uint64_t)1 << pos;
            jsp_char_masks got = jsp_classify_masks64(buf);
            assert(got.structural == want);
            assert(got.quote == 0);
            assert(got.backslash == 0);
        }
    }

    for (size_t pos = 0; pos < 64; pos++) {
        memset(buf, 'x', sizeof(buf));
        buf[pos] = '"';
        uint64_t       want = (uint64_t)1 << pos;
        jsp_char_masks got = jsp_classify_masks64(buf);
        assert(got.structural == want);
        assert(got.quote == want);
        assert(got.backslash == 0);
    }

    for (size_t pos = 0; pos < 64; pos++) {
        memset(buf, 'x', sizeof(buf));
        buf[pos] = '\\';
        jsp_char_masks got = jsp_classify_masks64(buf);
        assert(got.structural == 0);
        assert(got.quote == 0);
        assert(got.backslash == (uint64_t)1 << pos);
    }
}

/* Whichever implementation the dispatcher picked for this build must agree
   with the scalar oracle on all three masks, on random input. */
static void check_masks_random(void) {
    srand(67890);
    for (int trial = 0; trial < 1000; trial++) {
        uint8_t buf[64];
        for (size_t i = 0; i < sizeof(buf); i++) {
            buf[i] = (uint8_t)(rand() % 256);
        }

        jsp_char_masks got = jsp_classify_masks64(buf);
        jsp_char_masks want = jsp_classify_masks64_scalar(buf);
        assert(got.structural == want.structural);
        assert(got.quote == want.quote);
        assert(got.backslash == want.backslash);
    }
}

int main(void) {
    check_masks_directed();
    check_masks_random();

    printf("ok\n");
    return 0;
}
