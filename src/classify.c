#include "jsp_classify.h"

uint64_t jsp_classify64(const uint8_t *p) {
#if defined(JSP_FORCE_SCALAR)
    return jsp_classify64_scalar(p);
#elif defined(__ARM_NEON)
    return jsp_classify64_neon(p);
#elif defined(__AVX2__)
    return jsp_classify64_avx2(p);
#else
    return jsp_classify64_scalar(p);
#endif
}
