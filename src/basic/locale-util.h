/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include <libintl.h>    /* IWYU pragma: export */
#include <locale.h>     /* IWYU pragma: export */

#include "forward.h"

#define _(String) dgettext(GETTEXT_PACKAGE, String)
#define N_(String) String

bool is_locale_utf8(void);

static inline void freelocalep(locale_t *p) {
        if (*p == (locale_t) 0)
                return;

        freelocale(*p);
}
