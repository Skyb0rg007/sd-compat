/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include <signal.h>     /* IWYU pragma: export */

#include "forward.h"

int reset_all_signal_handlers(void);
int reset_signal_mask(void);

DECLARE_STRING_TABLE_LOOKUP(signal, int);

static inline void block_signals_reset(sigset_t **ss) {
        assert(ss);

        if (!*ss)
                return;

        assert_log(sigprocmask(SIG_SETMASK, *ss, NULL) >= 0);
}

#define SIGNO_INVALID (-EINVAL)

static inline bool SIGNAL_VALID(int signo) {
        return signo > 0 && signo < _NSIG;
}

int signal_is_blocked(int sig);

int autoreaping_enabled(void);

extern const struct sigaction sigaction_default;
