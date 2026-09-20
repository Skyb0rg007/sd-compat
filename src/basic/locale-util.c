/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <langinfo.h>

#include "env-util.h"
#include "locale-util.h"
#include "log.h"
#include "process-util.h"
#include "strv.h"

static bool is_locale_utf8_impl(void) {
        const char *set;
        int r;

        /* Note that we default to 'true' here, since today UTF8 is pretty much supported everywhere. */

        r = secure_getenv_bool("SYSTEMD_UTF8");
        if (r >= 0)
                return r;
        if (r != -ENXIO)
                log_debug_errno(r, "Failed to parse $SYSTEMD_UTF8, ignoring: %m");

        /* This function may be called from libsystemd, and setlocale() is not thread safe. Assuming yes. */
        if (!is_main_thread())
                return true;

        if (!setlocale(LC_ALL, ""))
                return true;

        set = nl_langinfo(CODESET);
        if (!set || streq(set, "UTF-8"))
                return true;

        set = setlocale(LC_CTYPE, NULL);
        if (!set)
                return true;

        /* Unless LC_CTYPE is explicitly overridden, return true. Because here CTYPE is effectively unset
         * and everything can do to UTF-8 nowadays. */
        return STR_IN_SET(set, "C", "POSIX") &&
                !getenv("LC_ALL") &&
                !getenv("LC_CTYPE") &&
                !getenv("LANG");
}

bool is_locale_utf8(void) {
        static int cached = -1;

        if (cached < 0)
                cached = is_locale_utf8_impl();

        return cached;
}

