/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "string-util.h"

sd_char *startswith_internal(const sd_char *s, const sd_char *prefix) {
        size_t l;

        assert(s);
        assert(prefix);

        l = strlen(prefix);
        if (!strneq(s, prefix, l))
                return NULL;

        return (sd_char*) s + l;
}

sd_char *startswith_no_case_internal(const sd_char *s, const sd_char *prefix) {
        size_t l;

        assert(s);
        assert(prefix);

        l = strlen(prefix);
        if (!strncaseeq(s, prefix, l))
                return NULL;

        return (sd_char*) s + l;
}

sd_char* endswith_internal(const sd_char *s, const sd_char *suffix) {
        size_t sl, pl;

        assert(s);
        assert(suffix);

        sl = strlen(s);
        pl = strlen(suffix);

        if (pl == 0)
                return (sd_char*) s + sl;

        if (sl < pl)
                return NULL;

        if (!streq(s + sl - pl, suffix))
                return NULL;

        return (sd_char*) s + sl - pl;
}

