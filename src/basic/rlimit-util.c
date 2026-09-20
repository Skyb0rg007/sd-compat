/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "fd-util.h"
#include "log.h"
#include "process-util.h"
#include "rlimit-util.h"

int rlimit_nofile_safe(void) {
        struct rlimit rl;

        /* Resets RLIMIT_NOFILE's soft limit FD_SETSIZE (i.e. 1024), for compatibility with software still using
         * select() */

        if (getrlimit(RLIMIT_NOFILE, &rl) < 0)
                return log_debug_errno(errno, "Failed to query RLIMIT_NOFILE: %m");

        if (rl.rlim_cur <= FD_SETSIZE)
                return 0;

        /* So we might have inherited a hard limit that's larger than the kernel's maximum limit as stored in
         * /proc/sys/fs/nr_open. If we pass this hard limit unmodified to setrlimit(), we'll get EPERM. To
         * make sure that doesn't happen, let's limit our hard limit to the value from nr_open. */
        rl.rlim_max = MIN(rl.rlim_max, (rlim_t) read_nr_open());
        rl.rlim_cur = MIN((rlim_t) FD_SETSIZE, rl.rlim_max);
        if (setrlimit(RLIMIT_NOFILE, &rl) < 0)
                return log_debug_errno(errno, "Failed to lower RLIMIT_NOFILE's soft limit to " RLIM_FMT ": %m", rl.rlim_cur);

        return 1;
}

