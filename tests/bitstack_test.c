/* jsp_bitstack in isolation: random push and pop sequences, filling the stack
   to capacity and draining it to empty, checked after every step against a
   plain array holding the same bits. */

#include "jsp_bitstack.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    srand(1);
    for (int run = 0; run < 2000; run++) {
        jsp_bitstack s = {0};
        bool         want[JSP_BITSTACK_CAPACITY];
        unsigned     n = 0;
        for (int step = 0; step < 500; step++) {
            bool push = n == 0 || (n < JSP_BITSTACK_CAPACITY && rand() % 5 < 3);
            if (push) {
                bool b = rand() & 1;
                jsp_bitstack_push(&s, b);
                want[n++] = b;
            } else {
                jsp_bitstack_pop(&s);
                n--;
            }
            assert(jsp_bitstack_depth(&s) == n);
            assert(jsp_bitstack_empty(&s) == (n == 0));
            assert(jsp_bitstack_full(&s) == (n == JSP_BITSTACK_CAPACITY));
            assert(n == 0 || jsp_bitstack_top(&s) == want[n - 1]);
            for (unsigned i = 0; i < n; i++) {
                assert(jsp_bitstack_at(&s, i) == want[i]);
            }
        }
    }

    printf("ok\n");
    return 0;
}
