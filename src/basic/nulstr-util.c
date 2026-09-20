/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "nulstr-util.h"
#include "string-util.h"
#include "strv.h"

char** strv_parse_nulstr_full(const char *s, size_t l, bool drop_trailing_nuls) {
        _cleanup_strv_free_ char **v = NULL;
        size_t c = 0, i = 0;

        /* l is the length of the input data, which will be split at NULs into elements of the resulting
         * strv. Hence, the number of items in the resulting strv will be equal to one plus the number of NUL
         * bytes in the l bytes starting at s, unless s[l-1] is NUL, in which case the final empty string is
         * not stored in the resulting strv, and length is equal to the number of NUL bytes.
         *
         * Note that contrary to a normal nulstr which cannot contain empty strings, because the input data
         * is terminated by any two consequent NUL bytes, this parser accepts empty strings in s. */

        assert(s || l <= 0);

        if (drop_trailing_nuls)
                while (l > 0 && s[l-1] == '\0')
                        l--;

        if (l <= 0)
                return new0(char*, 1);

        for (const char *p = s; p < s + l; p++)
                if (*p == 0)
                        c++;

        if (s[l-1] != 0)
                c++;

        v = new0(char*, c+1);
        if (!v)
                return NULL;

        for (const char *p = s; p < s + l;) {
                const char *e;

                e = memchr(p, 0, s + l - p);

                v[i] = memdup_suffix0(p, e ? e - p : s + l - p);
                if (!v[i])
                        return NULL;
                i++;

                if (!e)
                        break;

                p = e + 1;
        }

        assert(i == c);

        return TAKE_PTR(v);
}

char** strv_split_nulstr(const char *s) {
        _cleanup_strv_free_ char **l = NULL;

        /* This parses a nulstr, without specification of size, and stops at an empty string. This cannot
         * parse nulstrs with embedded empty strings hence, as an empty string is an end marker. Use
         * strv_parse_nulstr() above to parse a nulstr with embedded empty strings (which however requires a
         * size to be specified) */

        NULSTR_FOREACH(i, s)
                if (strv_extend(&l, i) < 0)
                        return NULL;

        return l ? TAKE_PTR(l) : strv_new(NULL);
}

