/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include <signal.h>     /* IWYU pragma: export */

#include "forward.h"

int reset_all_signal_handlers(void);
int reset_signal_mask(void);

int sigaction_many_internal(const struct sigaction *sa, ...);

#define ignore_signals(...)                                             \
        sigaction_many_internal(                                        \
                        &sigaction_ignore,                              \
                        __VA_ARGS__,                                    \
                        -1)

#define default_signals(...)                                            \
        sigaction_many_internal(                                        \
                        &sigaction_default,                             \
                        __VA_ARGS__,                                    \
                        -1)

#define sigaction_many(sa, ...)                                         \
        sigaction_many_internal(sa, __VA_ARGS__, -1)

int sigset_add_many_internal(sigset_t *ss, ...);
#define sigset_add_many(...) sigset_add_many_internal(__VA_ARGS__, -1)

int sigprocmask_many_internal(int how, sigset_t *ret_old_mask, ...);
#define sigprocmask_many(...) sigprocmask_many_internal(__VA_ARGS__, -1)

DECLARE_STRING_TABLE_LOOKUP(signal, int);
const char* signal_code_to_string(int signo, int code) _const_;

static inline void block_signals_reset(sigset_t **ss) {
        assert(ss);

        if (!*ss)
                return;

        assert_log(sigprocmask(SIG_SETMASK, *ss, NULL) >= 0);
}

#define BLOCK_SIGNALS(...)                                              \
        sigset_t _saved_sigset;                                         \
        _cleanup_(block_signals_reset) _unused_ sigset_t *_saved_sigsetp = \
                assert_log(sigprocmask_many(SIG_BLOCK, &_saved_sigset, __VA_ARGS__) >= 0) ? \
                &_saved_sigset : NULL;

#define SIGNO_INVALID (-EINVAL)

static inline bool SIGNAL_VALID(int signo) {
        return signo > 0 && signo < _NSIG;
}

int signal_is_blocked(int sig);

int autoreaping_enabled(void);

int pop_pending_signal_internal(int sig, ...);
#define pop_pending_signal(...) pop_pending_signal_internal(__VA_ARGS__, -1)

extern const struct sigaction sigaction_ignore;
extern const struct sigaction sigaction_default;
extern const struct sigaction sigaction_nop_nocldstop;

