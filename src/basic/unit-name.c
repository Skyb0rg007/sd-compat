/* SPDX-License-Identifier: LGPL-2.1-or-later */


#include "string-util.h"
#include "unit-def.h"
#include "unit-name.h"

/* Characters valid in a unit name. */
#define VALID_CHARS                             \
        ALPHANUMERICAL                          \
        ":-_.\\"

/* The same, but also permits the single @ character that may appear */
#define VALID_CHARS_WITH_AT                     \
        "@"                                     \
        VALID_CHARS

/* All chars valid in a unit name glob */
#define VALID_CHARS_GLOB                        \
        VALID_CHARS_WITH_AT                     \
        "[]!-*?"

#define LONG_UNIT_NAME_HASH_KEY SD_ID128_MAKE(ec,f2,37,fb,58,32,4a,32,84,9f,06,9b,0d,21,eb,9a)
#define UNIT_NAME_HASH_LENGTH_CHARS 16

bool unit_name_is_valid(const char *n, UnitNameFlags flags) {
        const char *e, *i, *at;

        assert((flags & ~(UNIT_NAME_PLAIN|UNIT_NAME_INSTANCE|UNIT_NAME_TEMPLATE)) == 0);

        if (_unlikely_(flags == 0))
                return false;

        if (isempty(n))
                return false;

        if (strlen(n) >= UNIT_NAME_MAX)
                return false;

        e = strrchr(n, '.');
        if (!e || e == n)
                return false;

        if (unit_type_from_string(e + 1) < 0)
                return false;

        for (i = n, at = NULL; i < e; i++) {

                if (*i == '@' && !at)
                        at = i;

                if (!strchr(VALID_CHARS_WITH_AT, *i))
                        return false;
        }

        if (at == n)
                return false;

        if (flags & UNIT_NAME_PLAIN)
                if (!at)
                        return true;

        if (flags & UNIT_NAME_INSTANCE)
                if (at && e > at + 1)
                        return true;

        if (flags & UNIT_NAME_TEMPLATE)
                if (at && e == at + 1)
                        return true;

        return false;
}

UnitNameFlags unit_name_to_instance(const char *n, char **ret) {
        const char *p, *d;

        assert(n);

        if (!unit_name_is_valid(n, UNIT_NAME_ANY))
                return -EINVAL;

        /* Everything past the first @ and before the last . is the instance */
        p = strchr(n, '@');
        if (!p) {
                if (ret)
                        *ret = NULL;
                return UNIT_NAME_PLAIN;
        }

        p++;

        d = strrchr(p, '.');
        if (!d)
                return -EINVAL;

        if (ret) {
                char *i = strndup(p, d-p);
                if (!i)
                        return -ENOMEM;

                *ret = i;
        }
        return d > p ? UNIT_NAME_INSTANCE : UNIT_NAME_TEMPLATE;
}

/**
 *  Convert a string to a unit name. /dev/blah is converted to dev-blah.device,
 *  /blah/blah is converted to blah-blah.mount, anything else is left alone,
 *  except that @suffix is appended if a valid unit suffix is not present.
 *
 *  If @allow_globs, globs characters are preserved. Otherwise, they are escaped.
 */
