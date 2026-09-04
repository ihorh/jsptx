#include "jsp_classify.h"

uint64_t jsp_classify64(const uint8_t *p) {
#if defined(JSP_FORCE_SCALAR)
    return jsp_classify64_scalar(p);
#elif defined(__ARM_NEON)
    return jsp_classify64_neon(p);
#else
    /* The AVX2 arm lands here once it exists; the scalar version is the
       fallback for everything else. */
    return jsp_classify64_scalar(p);
#endif
}
