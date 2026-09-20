/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include "forward.h"

extern int saved_argc;
extern char **saved_argv;

int rename_process_full(const char *comm, const char *invocation);
static inline int rename_process(const char *name) {
        return rename_process_full(name, name);
}
