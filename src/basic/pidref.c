/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <poll.h>
#include <sys/wait.h>

#include "errno-util.h"
#include "fd-util.h"
#include "fiber-ops.h"
#include "io-util.h"
#include "log.h"
#include "pidfd-util.h"
#include "pidref.h"
#include "process-util.h"
#include "time-util.h"

int pidref_acquire_pidfd_id(PidRef *pidref) {
        int r;

        assert(pidref);

        if (!pidref_is_set(pidref))
                return -ESRCH;

        if (pidref_is_remote(pidref))
                return -EREMOTE;

        if (pidref->fd < 0)
                return -ENOMEDIUM;

        if (pidref->fd_id > 0)
                return 0;

        r = pidfd_get_inode_id(pidref->fd, &pidref->fd_id);
        if (r < 0) {
                if (!ERRNO_IS_NEG_NOT_SUPPORTED(r))
                        log_debug_errno(r, "Failed to get inode number of pidfd for pid " PID_FMT ": %m",
                                        pidref->pid);
                return r;
        }

        return 0;
}

int pidref_set_pid_full(PidRef *pidref, pid_t pid, unsigned flags) {
        uint64_t pidfdid = 0;
        int fd;

        assert(pidref);

        if (pid < 0)
                return -ESRCH;
        if (pid == 0) {
                pid = getpid_cached();
                (void) pidfd_get_inode_id_self_cached(&pidfdid);
        }

        fd = pidfd_open(pid, flags);
        if (fd < 0) {
                /* Graceful fallback in case the kernel is out of fds.
                 * The PIDFD_THREAD flag is supported since kernel v6.9 (64bef697d33b75fc06c5789b3f8108680271529f). */
                if (!(ERRNO_IS_RESOURCE(errno) || (errno == EINVAL && FLAGS_SET(flags, PIDFD_THREAD))))
                        return log_debug_errno(errno, "Failed to open pidfd for pid " PID_FMT ": %m", pid);

                fd = -EBADF;
        }

        *pidref = (PidRef) {
                .fd = fd,
                .pid = pid,
                .fd_id = pidfdid,
        };

        return 0;
}

int pidref_set_pidfd(PidRef *pidref, int fd) {
        int r;

        assert(pidref);

        if (fd < 0)
                return -EBADF;

        int fd_copy = fcntl(fd, F_DUPFD_CLOEXEC, 3);
        if (fd_copy < 0) {
                pid_t pid;

                if (!ERRNO_IS_RESOURCE(errno))
                        return -errno;

                /* Graceful fallback if we are out of fds */
                r = pidfd_get_pid(fd, &pid);
                if (r < 0)
                        return r;

                *pidref = PIDREF_MAKE_FROM_PID(pid);
                return 0;
        }

        return pidref_set_pidfd_consume(pidref, fd_copy);
}

int pidref_set_pidfd_take(PidRef *pidref, int fd) {
        pid_t pid;
        int r;

        assert(pidref);

        if (fd < 0)
                return -EBADF;

        r = pidfd_get_pid(fd, &pid);
        if (r < 0)
                return r;

        *pidref = (PidRef) {
                .fd = fd,
                .pid = pid,
        };

        return 0;
}

int pidref_set_pidfd_consume(PidRef *pidref, int fd) {
        int r;

        r = pidref_set_pidfd_take(pidref, fd);
        if (r < 0)
                safe_close(fd);

        return r;
}

void pidref_done(PidRef *pidref) {
        assert(pidref);

        *pidref = (PidRef) {
                .fd = safe_close(pidref->fd),
        };
}

int pidref_kill(const PidRef *pidref, int sig) {

        if (!pidref)
                return -ESRCH;

        if (pidref_is_remote(pidref))
                return -EREMOTE;

        if (pidref->fd >= 0)
                return RET_NERRNO(pidfd_send_signal(pidref->fd, sig, NULL, 0));

        if (pidref->pid > 0)
                return RET_NERRNO(kill(pidref->pid, sig));

        return -ESRCH;
}

int pidref_verify(const PidRef *pidref) {
        int r;

        /* This is a helper that is supposed to be called after reading information from procfs via a
         * PidRef. It ensures that the PID we track still matches the PIDFD we pin. If this value differs
         * after a procfs read, we might have read the data from a recycled PID. */

        if (!pidref_is_set(pidref))
                return -ESRCH;

        if (pidref_is_remote(pidref))
                return -EREMOTE;

        if (pidref->pid == 1)
                return 1; /* PID 1 can never go away, hence never be recycled to a different process → return 1 */

        if (pidref->fd < 0)
                return 0; /* If we don't have a pidfd we cannot validate it, hence we assume it's all OK → return 0 */

        r = pidfd_verify_pid(pidref->fd, pidref->pid);
        if (r < 0)
                return r;

        return 1; /* We have a pidfd and it still points to the PID we have, hence all is *really* OK → return 1 */
}

bool pidref_is_self(PidRef *pidref) {
        if (!pidref_is_set(pidref))
                return false;

        if (pidref_is_remote(pidref))
                return false;

        if (pidref->pid != getpid_cached())
                return false;

        /* PID1 cannot exit, hence no point in comparing pidfd IDs, they can never change */
        if (pidref->pid == 1)
                return true;

        /* Also compare pidfd ID if we can get it */
        if (pidref_acquire_pidfd_id(pidref) < 0)
                return true;

        uint64_t self_id;
        if (pidfd_get_inode_id_self_cached(&self_id) < 0)
                return true;

        return pidref->fd_id == self_id;
}

int pidref_wait_for_terminate_full(PidRef *pidref, usec_t timeout, siginfo_t *ret_si) {
        int r;

        assert(timeout > 0);

        if (!pidref_is_set(pidref))
                return -ESRCH;

        if (pidref_is_remote(pidref))
                return -EREMOTE;

        if (pidref->pid == 1 || pidref_is_self(pidref))
                return -ECHILD;

        if (pidref->fd < 0 && (timeout != USEC_INFINITY || fiber_ops_is_set()))
                return -ENOMEDIUM;

        usec_t ts = timeout == USEC_INFINITY ? USEC_INFINITY : usec_add(now(CLOCK_MONOTONIC), timeout);

        /* Poll the pidfd before waitid() if either there's a finite timeout (so we can honor it) or
         * we're on a fiber (so fd_wait_for_event() can suspend us instead of blocking the event loop
         * inside waitid()). Otherwise let waitid() block directly. The precondition above guarantees
         * pidref->fd >= 0 in both cases. */
        bool poll_first = ts != USEC_INFINITY || fiber_ops_is_set();

        for (;;) {
                if (poll_first) {
                        usec_t left;

                        if (ts == USEC_INFINITY)
                                left = USEC_INFINITY;
                        else {
                                left = usec_sub_unsigned(ts, now(CLOCK_MONOTONIC));
                                if (left == 0)
                                        return -ETIMEDOUT;
                        }

                        r = fd_wait_for_event(pidref->fd, POLLIN, left);
                        if (r == 0)
                                return -ETIMEDOUT;
                        if (r == -EINTR)
                                continue;
                        if (r < 0)
                                return r;
                }

                siginfo_t si = {};

                if (pidref->fd >= 0)
                        r = RET_NERRNO(waitid(P_PIDFD, pidref->fd, &si, WEXITED));
                else
                        r = RET_NERRNO(waitid(P_PID, pidref->pid, &si, WEXITED));
                if (r >= 0) {
                        if (ret_si)
                                *ret_si = si;
                        return 0;
                }
                if (r != -EINTR)
                        return r;
        }
}

