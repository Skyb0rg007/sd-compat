/* SPDX-License-Identifier: LGPL-2.1-or-later */


#include "parse-util.h"
#include "string-util.h"

const char* string_table_lookup_to_string(const char * const *table, size_t len, ssize_t i) {
        if (i < 0 || i >= (ssize_t) len)
                return NULL;

        assert(table);

        return table[i];
}

ssize_t string_table_lookup_from_string(const char * const *table, size_t len, const char *key) {
        assert_return(table, -EINVAL);

        if (!key)
                return -EINVAL;

        for (size_t i = 0; i < len; ++i)
                if (streq_ptr(table[i], key))
                        return (ssize_t) i;

        return -EINVAL;
}

ssize_t string_table_lookup_from_string_with_boolean(const char * const *table, size_t len, const char *key, ssize_t yes) {
        if (!key)
                return -EINVAL;

        int b = parse_boolean(key);
        if (b == 0)
                return 0;
        if (b > 0)
                return yes;

        return string_table_lookup_from_string(table, len, key);
}

