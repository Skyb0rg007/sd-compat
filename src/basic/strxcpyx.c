/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*
 * Concatenates/copies strings. In any case, terminates in all cases
 * with '\0' and moves the @dest pointer forward to the added '\0'.
 * Returns the remaining size, and 0 if the string was truncated.
 *
 * Due to the intended usage, these helpers silently noop invocations
 * having zero size.  This is technically an exception to the above
 * statement "terminates in all cases".  It's unexpected for such calls to
 * occur outside of a loop where this is the preferred behavior.
 */

#include <stdio.h>

#include "strxcpyx.h"

size_t strpcpyf_full(char **dest, size_t size, bool *ret_truncated, const char *src, ...) {
        bool truncated = false;
        va_list va;
        int i;

        assert(dest);
        assert(src);

        va_start(va, src);
        i = vsnprintf(*dest, size, src, va);
        va_end(va);

        assert(i >= 0);

        if (i < (int) size) {
                *dest += i;
                size -= i;
        } else {
                size = 0;
                truncated = i > 0;
        }

        if (ret_truncated)
                *ret_truncated = truncated;

        return size;
}

