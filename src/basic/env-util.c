/* SPDX-License-Identifier: LGPL-2.1-or-later */


#include "errno-util.h"
#include "parse-util.h"
#include "process-util.h"
#include "strv.h"

/* We follow bash for the character set. Different shells have different rules. */
#define VALID_BASH_ENV_NAME_CHARS               \
        ALPHANUMERICAL                          \
        "_"

int strv_env_get_merged(char **l, char ***ret) {
        _cleanup_strv_free_ char **v = NULL;
        size_t n = 0;
        int r;

        assert(ret);

        /* This converts a strv with pairs of environment variable name + value into a strv of name and
         * value concatenated with a "=" separator. E.g.
         * input  : { "NAME", "value", "FOO", "var" }
         * output : { "NAME=value", "FOO=var" } */

        STRV_FOREACH_PAIR(key, value, l) {
                char *s;

                s = strjoin(*key, "=", *value);
                if (!s)
                        return -ENOMEM;

                r = strv_consume_with_size(&v, &n, s);
                if (r < 0)
                        return r;
        }

        *ret = TAKE_PTR(v);
        return 0;
}

int getenv_bool(const char *p) {
        const char *e;

        e = getenv(p);
        if (!e)
                return -ENXIO;

        return parse_boolean(e);
}

int secure_getenv_bool(const char *p) {
        const char *e;

        e = secure_getenv(p);
        if (!e)
                return -ENXIO;

        return parse_boolean(e);
}

int setenvf(const char *name, bool overwrite, const char *valuef, ...) {
        _cleanup_free_ char *value = NULL;
        va_list ap;
        int r;

        assert(name);

        if (!valuef)
                return RET_NERRNO(unsetenv(name));

        va_start(ap, valuef);
        r = vasprintf(&value, valuef, ap);
        va_end(ap);

        if (r < 0)
                return -ENOMEM;

        /* Try to suppress writes if the value is already set correctly (simply because memory management of
         * environment variables sucks a bit. */
        if (streq_ptr(getenv(name), value))
                return 0;

        return RET_NERRNO(setenv(name, value, overwrite));
}
