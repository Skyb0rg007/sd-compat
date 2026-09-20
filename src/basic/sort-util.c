/* SPDX-License-Identifier: LGPL-2.1-or-later */


#include "alloc-util.h"

/* hey glibc, APIs with callbacks without a user pointer are so useless */
void qsort_safe(void *base, size_t nmemb, size_t size, comparison_fn_t compar) {
        /**
         * Normal qsort requires base to be nonnull. Here were require
         * that only if nmemb > 0.
         */

        if (nmemb <= 1)
                return;

        assert(base);
        qsort(base, nmemb, size, compar);
}

void qsort_r_safe(void *base, size_t nmemb, size_t size, comparison_userdata_fn_t compar, void *userdata) {
        if (nmemb <= 1)
                return;

        assert(base);
        qsort_r(base, nmemb, size, compar, userdata);
}

int cmp_int(const int *a, const int *b) {
        /* This is called from qsort()s inner loops. Correctly implemented qsort will never pass NULL so we
           just suppress the check via POINTER_MAY_BE_NULL instead of assert() to avoid the runtime cost. */
        POINTER_MAY_BE_NULL(a);
        POINTER_MAY_BE_NULL(b);

        return CMP(*a, *b);
}

