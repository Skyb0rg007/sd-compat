/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <poll.h>
#include <sys/epoll.h>          /* IWYU pragma: keep */

#include "sd-event.h"
#include "sd-future.h"

#include "alloc-util.h"
#include "errno-util.h"
#include "event-future.h"
#include "fd-util.h"
#include "io-util.h"
#include "time-util.h"

typedef ssize_t (*FiberIOFunc)(int fd, void *args);

static ssize_t fiber_io_operation(
                int fd,
                uint32_t events,
                FiberIOFunc func,
                void *args) {
        _cleanup_(nonblock_resetp) int reset_fd = -EBADF;
        int r;

        assert(fd >= 0);
        assert(func);

        if (!sd_fiber_is_running())
                return func(fd, args);

        sd_event *e = sd_fiber_get_event();
        assert(e);

        r = fd_nonblock(fd, true);
        if (r < 0)
                return r;
        if (r > 0)
                reset_fd = fd;

        ssize_t n = func(fd, args);
        if (n >= 0 || !ERRNO_IS_NEG_TRANSIENT(n))
                return n;

        _cleanup_(sd_future_cancel_wait_unrefp) sd_future *io = NULL;
        r = future_new_io(e, fd, events, &io);
        if (r < 0)
                return r;

        r = sd_fiber_suspend();
        if (r < 0)
                return r;

        return func(fd, args);
}

typedef struct ReadArgs {
        void *buf;
        size_t count;
} ReadArgs;

static ssize_t read_callback(int fd, void *args) {
        ReadArgs *a = ASSERT_PTR(args);
        ssize_t n;

        n = read(fd, a->buf, a->count);
        return n >= 0 ? n : -errno;
}

ssize_t sd_fiber_read(int fd, void *buf, size_t count) {
        assert_return(fd >= 0, -EBADF);
        assert_return(buf || count == 0, -EINVAL);

        return fiber_io_operation(fd, EPOLLIN, read_callback, &(ReadArgs) {
                .buf = buf,
                .count = count,
        });
}

typedef struct WriteArgs {
        const void *buf;
        size_t count;
} WriteArgs;

static ssize_t write_callback(int fd, void *args) {
        WriteArgs *a = ASSERT_PTR(args);
        ssize_t n;

        n = write(fd, a->buf, a->count);
        return n >= 0 ? n : -errno;
}

ssize_t sd_fiber_write(int fd, const void *buf, size_t count) {
        assert_return(fd >= 0, -EBADF);
        assert_return(buf || count == 0, -EINVAL);

        return fiber_io_operation(fd, EPOLLOUT, write_callback, &(WriteArgs) {
                .buf = buf,
                .count = count,
        });
}

typedef struct ReadvArgs {
        const struct iovec *iov;
        int iovcnt;
} ReadvArgs;

typedef struct WritevArgs {
        const struct iovec *iov;
        int iovcnt;
} WritevArgs;

typedef struct RecvArgs {
        void *buf;
        size_t len;
        int flags;
} RecvArgs;

typedef struct SendArgs {
        const void *buf;
        size_t len;
        int flags;
} SendArgs;

typedef struct RecvmsgArgs {
        struct msghdr *msg;
        int flags;
} RecvmsgArgs;

typedef struct SendmsgArgs {
        const struct msghdr *msg;
        int flags;
} SendmsgArgs;

typedef struct AcceptArgs {
        struct sockaddr *addr;
        socklen_t *addrlen;
        int flags;
} AcceptArgs;

int sd_fiber_ppoll(struct pollfd *fds, size_t n_fds, const struct timespec *timeout, const sigset_t *sigmask) {
        int r;

        assert_return(fds || n_fds == 0, -EINVAL);

        if (!sd_fiber_is_running())
                return RET_NERRNO(ppoll(fds, n_fds, timeout, sigmask));

        /* When on a fiber signals are handled via sd-event hence we should never mess around with the
         * signal mask when running on a fiber. */
        assert_return(!sigmask, -EOPNOTSUPP);

        sd_event *e = sd_fiber_get_event();
        assert(e);

        /* No fds to wait on and no timeout means there's nothing that could ever wake the fiber up,
         * since unlike raw ppoll() we cannot use signal delivery as a wakeup. Signals received while
         * the fiber is suspended are handled by sd-event via signalfd, in which case the signal handler
         * is expected to cancel the fiber via sd_future_cancel() if a wakeup is desired. */
        if (n_fds == 0 && !timeout)
                return -EINVAL;

        bool zero_timeout = timeout && timeout->tv_sec == 0 && timeout->tv_nsec == 0;

        /* Try polling with zero timeout first to see if any are immediately ready. */
        r = RET_NERRNO(ppoll(fds, n_fds, &(const struct timespec) {}, /* sigmask= */ NULL));
        if (zero_timeout || r != 0) /* Either error or some fds are ready */
                return r;

        sd_future **futures = NULL;
        CLEANUP_ARRAY(futures, n_fds, sd_future_cancel_wait_unref_array);

        futures = new0(sd_future*, n_fds);
        if (!futures)
                return -ENOMEM;

        /* Set up I/O event sources for all valid fds. POLL* and EPOLL* share their bit values (see
         * EPOLL_POLL_COMMON_MASK in io-util.h), so we can pass the user-supplied event mask through
         * to either backend without translation. */
        size_t n_io_futures = 0;
        for (size_t i = 0; i < n_fds; i++) {
                if (fds[i].fd < 0)
                        continue;

                uint32_t events = poll_events_to_epoll(fds[i].events);
                if (events == 0)
                        continue;

                r = future_new_io(e, fds[i].fd, events, &futures[i]);
                if (r < 0)
                        return r;

                n_io_futures++;
        }

        /* A timeout that overflows usec_t saturates to USEC_INFINITY in timespec_load(); treat that
         * like "no timeout" (matches sd_fiber_sleep(USEC_INFINITY)) rather than letting
         * sd_event_add_time_relative() reject it with -EOVERFLOW — standard ppoll() would just
         * wait a very long time. */
        usec_t usec = timeout ? timespec_load(timeout) : USEC_INFINITY;

        /* If every fd was skipped (negative or empty event mask) and we'd have no timer, there's
         * nothing that could ever wake the fiber up — same situation as n_fds == 0 && !timeout,
         * just not detectable upfront. Refuse rather than suspend forever. */
        if (n_io_futures == 0 && usec == USEC_INFINITY)
                return -EINVAL;

        _cleanup_(sd_future_cancel_wait_unrefp) sd_future *timer = NULL;
        if (usec != USEC_INFINITY) {
                r = future_new_time_relative(
                                e,
                                CLOCK_MONOTONIC,
                                usec,
                                /* accuracy= */ 1,
                                /* result= */ 0,
                                &timer);
                if (r < 0)
                        return r;
        }

        r = sd_fiber_suspend();
        if (r < 0 && r != -ETIME)
                return r;

        /* Always sweep fds with a non-blocking ppoll(): the timer and an fd readiness can resolve in
         * the same event-loop tick (or the fd can become ready between the timer firing and us being
         * scheduled), and ppoll() semantics give events precedence over the timeout in that case. */
        int n = RET_NERRNO(ppoll(fds, n_fds, &(const struct timespec) {}, /* sigmask= */ NULL));
        if (n != 0)
                return n;

        /* No fds ready: distinguish our own timer from an external -ETIME. */
        if (timer && sd_future_state(timer) == SD_FUTURE_RESOLVED)
                return 0;

        /* An IO future resolved with a revents mask (r > 0) but the readiness was already consumed
         * by the time we swept — report 0 rather than leaking the bitmask as a (bogus) ppoll fd
         * count to the caller. */
        if (r > 0)
                return 0;

        return r;
}
