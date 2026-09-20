/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include <sys/resource.h>       /* IWYU pragma: export */

#include "forward.h"

#define _RLIMIT_MAX RLIMIT_NLIMITS

DECLARE_STRING_TABLE_LOOKUP(rlimit, int);
int rlimit_from_string_harder(const char *s) _pure_;

#define RLIMIT_MAKE_CONST(lim) ((struct rlimit) { lim, lim })

int rlimit_nofile_safe(void);

