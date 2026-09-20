/* SPDX-License-Identifier: LGPL-2.1-or-later */


#include "hostname-util.h"
#include "string-util.h"
#include "user-util.h"

bool valid_ldh_char(char c) {
        /* "LDH" → "Letters, digits, hyphens", as per RFC 5890, Section 2.3.1 */

        return ascii_isalpha(c) ||
                ascii_isdigit(c) ||
                c == '-';
}

bool hostname_is_valid(const char *s, ValidHostnameFlags flags) {
        unsigned n_dots = 0;
        const char *p;
        bool dot, hyphen;

        /* Check if s looks like a valid hostname or FQDN. This does not do full DNS validation, but only
         * checks if the name is composed of allowed characters and the length is not above the maximum
         * allowed by Linux (c.f. dns_name_is_valid()). A trailing dot is allowed if
         * VALID_HOSTNAME_TRAILING_DOT flag is set and at least two components are present in the name. Note
         * that due to the restricted charset and length this call is substantially more conservative than
         * dns_name_is_valid(). Doesn't accept empty hostnames, hostnames with leading dots, and hostnames
         * with multiple dots in a sequence. Doesn't allow hyphens at the beginning or end of label. */

        if (isempty(s))
                return false;

        if (streq(s, ".host")) /* Used by the container logic to denote the "root container" */
                return FLAGS_SET(flags, VALID_HOSTNAME_DOT_HOST);

        for (p = s, dot = hyphen = true; *p; p++)
                if (*p == '.') {
                        if (dot || hyphen)
                                return false;

                        dot = true;
                        hyphen = false;
                        n_dots++;

                } else if (*p == '-') {
                        if (dot)
                                return false;

                        dot = false;
                        hyphen = true;

                } else {
                        if (!valid_ldh_char(*p) &&
                            (*p != '?' || !FLAGS_SET(flags, VALID_HOSTNAME_QUESTION_MARK)) &&
                            (*p != '$' || !FLAGS_SET(flags, VALID_HOSTNAME_WORD_TOKEN)))
                                return false;

                        dot = false;
                        hyphen = false;
                }

        if (dot && (n_dots < 2 || !FLAGS_SET(flags, VALID_HOSTNAME_TRAILING_DOT)))
                return false;
        if (hyphen)
                return false;

        /* Note that host name max is 64 on Linux, but DNS allows domain names up to 255 characters. */
        if (p - s > (ssize_t) LINUX_HOST_NAME_MAX)
                return false;

        return true;
}

int split_user_at_host(const char *s, char **ret_user, char **ret_host) {
        _cleanup_free_ char *u = NULL, *h = NULL;

        /* Splits a user@host expression (one of those we accept on --machine= and similar). Returns NULL in
         * each of the two return parameters if that part was left empty. */

        assert(s);

        const char *rhs = strchr(s, '@');
        if (rhs) {
                if (ret_user && rhs > s) {
                        u = strndup(s, rhs - s);
                        if (!u)
                                return -ENOMEM;
                }

                if (ret_host && rhs[1] != 0) {
                        h = strdup(rhs + 1);
                        if (!h)
                                return -ENOMEM;
                }

        } else {
                if (isempty(s))
                        return -EINVAL;

                if (ret_host) {
                        h = strdup(s);
                        if (!h)
                                return -ENOMEM;
                }
        }

        if (ret_user)
                *ret_user = TAKE_PTR(u);
        if (ret_host)
                *ret_host = TAKE_PTR(h);

        return !!rhs; /* return > 0 if '@' was specified, 0 otherwise */
}

int machine_spec_valid(const char *s) {
        _cleanup_free_ char *u = NULL, *h = NULL;
        int r;

        assert(s);

        r = split_user_at_host(s, &u, &h);
        if (r == -EINVAL)
                return false;
        if (r < 0)
                return r;

        if (u && !valid_user_group_name(u, VALID_USER_RELAX | VALID_USER_ALLOW_NUMERIC))
                return false;

        if (h && !hostname_is_valid(h, VALID_HOSTNAME_DOT_HOST))
                return false;

        return true;
}

