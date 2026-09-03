#include "jsp_classify.h"

uint64_t jsp_classify64(const uint8_t *p) {
#if defined(JSP_FORCE_SCALAR)
    return jsp_classify64_scalar(p);
#else
    /* AVX2/NEON arms land here once they exist; the scalar version is the
       fallback for everything else, so it is also what "auto" means today. */
    return jsp_classify64_scalar(p);
#endif
}
