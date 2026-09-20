/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <pthread.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#if HAVE_VALGRIND_VALGRIND_H
#endif


#include "argv-util.h"
#include "dlfcn-util.h"
#include "fd-util.h"
#include "fs-util.h"
#include "io-util.h"
#include "iovec-util.h"
#include "log.h"
#include "mountpoint-util.h"
#include "namespace-util.h"
#include "parse-util.h"
#include "pidref.h"
#include "process-util.h"
#include "rlimit-util.h"
#include "signal-util.h"
#include "socket-util.h"
#include "stat-util.h"
#include "user-util.h"

/* The kernel limits userspace processes to TASK_COMM_LEN (16 bytes), but allows higher values for its own
 * workers, e.g. "kworker/u9:3-kcryptd/253:0". Let's pick a fixed smallish limit that will work for the kernel.
 */
#define COMM_MAX_LEN 128

int pid_get_comm(pid_t pid, char **ret) {
        _cleanup_free_ char *escaped = NULL, *comm = NULL;
        int r;

        assert(pid >= 0);
        assert(ret);

        if (pid == 0 || pid == getpid_cached()) {
                comm = new0(char, TASK_COMM_LEN + 1); /* Must fit in 16 byte according to prctl(2) */
                if (!comm)
                        return -ENOMEM;

                r = prctl_safe(PR_GET_NAME, (unsigned long) comm, 0, 0, 0);
                if (r < 0)
                        return r;
        } else {
                const char *p;

                p = procfs_file_alloca(pid, "comm");

                /* Note that process names of kernel threads can be much longer than TASK_COMM_LEN */
                r = read_one_line_file(p, &comm);
                if (r == -ENOENT)
                        return -ESRCH;
                if (r < 0)
                        return r;
        }

        escaped = new(char, COMM_MAX_LEN);
        if (!escaped)
                return -ENOMEM;

        /* Escape unprintable characters, just in case, but don't grow the string beyond the underlying size */
        cellescape(escaped, COMM_MAX_LEN, comm);

        *ret = TAKE_PTR(escaped);
        return 0;
}

int pidref_get_comm(const PidRef *pid, char **ret) {
        _cleanup_free_ char *comm = NULL;
        int r;

        if (!pidref_is_set(pid))
                return -ESRCH;

        if (pidref_is_remote(pid))
                return -EREMOTE;

        r = pid_get_comm(pid->pid, &comm);
        if (r < 0)
                return r;

        r = pidref_verify(pid);
        if (r < 0)
                return r;

        if (ret)
                *ret = TAKE_PTR(comm);
        return 0;
}

static int get_process_link_contents(pid_t pid, const char *proc_file, char **ret) {
        const char *p;
        int r;

        assert(proc_file);

        p = procfs_file_alloca(pid, proc_file);

        r = readlink_malloc(p, ret);
        return (r == -ENOENT && proc_mounted() > 0) ? -ESRCH : r;
}

int get_process_exe(pid_t pid, char **ret) {
        char *d;
        int r;

        assert(pid >= 0);

        r = get_process_link_contents(pid, "exe", ret);
        if (r < 0)
                return r;

        if (ret) {
                d = endswith(*ret, " (deleted)");
                if (d)
                        *d = '\0';
        }

        return 0;
}

#define ENVIRONMENT_BLOCK_MAX (5U*1024U*1024U)

/*
 * Return values:
 * < 0 : pidref_wait_for_terminate() failed to get the state of the
 *       process, the process was terminated by a signal, or
 *       failed for an unknown reason.
 * >=0 : The process terminated normally, and its exit code is
 *       returned.
 *
 * That is, success is indicated by a return value of zero, and an
 * error is indicated by a non-zero value.
 *
 * A warning is emitted if the process terminates abnormally,
 * and also if it returns non-zero unless check_exit_code is true.
 */
int pidref_wait_for_terminate_and_check(const char *name, PidRef *pidref, WaitFlags flags) {
        assert((flags & WAIT_UNSUPPORTED) == 0);

        int r;

        if (!pidref_is_set(pidref))
                return -ESRCH;
        if (pidref_is_remote(pidref))
                return -EREMOTE;
        if (pidref->pid == 1 || pidref_is_self(pidref))
                return -ECHILD;

        _cleanup_free_ char *buffer = NULL;
        if (!name) {
                r = pidref_get_comm(pidref, &buffer);
                if (r < 0)
                        log_debug_errno(r, "Failed to acquire process name of " PID_FMT ", ignoring: %m", pidref->pid);
                else
                        name = buffer;
        }

        int prio = flags & WAIT_LOG_ABNORMAL ? LOG_ERR : LOG_DEBUG;

        siginfo_t status;
        r = pidref_wait_for_terminate(pidref, &status);
        if (r < 0)
                return log_debug_errno(r, "Failed to wait for '%s': %m", strna(name));

        if (status.si_code == CLD_EXITED) {
                if (status.si_status != EXIT_SUCCESS)
                        log_full(flags & WAIT_LOG_NON_ZERO_EXIT_STATUS ? LOG_ERR : LOG_DEBUG,
                                 "'%s' failed with exit status %i.", strna(name), status.si_status);
                else
                        log_debug("'%s' succeeded.", name);

                return status.si_status;

        } else if (IN_SET(status.si_code, CLD_KILLED, CLD_DUMPED))
                return log_full_errno(prio, SYNTHETIC_ERRNO(EPROTO),
                                      "'%s' terminated by signal %s.", strna(name), signal_to_string(status.si_status));

        return log_full_errno(prio, SYNTHETIC_ERRNO(EPROTO),
                              "'%s' failed due to unknown reason.", strna(name));
}

int getenv_for_pid(pid_t pid, const char *field, char **ret) {
        _cleanup_fclose_ FILE *f = NULL;
        const char *path;
        size_t sum = 0;
        int r;

        assert(pid >= 0);
        assert(field);
        assert(ret);

        if (pid == 0 || pid == getpid_cached())
                return strdup_to_full(ret, getenv(field));

        if (!pid_is_valid(pid))
                return -EINVAL;

        path = procfs_file_alloca(pid, "environ");

        r = fopen_unlocked(path, "re", &f);
        if (r == -ENOENT)
                return -ESRCH;
        if (r < 0)
                return r;

        for (;;) {
                _cleanup_free_ char *line = NULL;
                const char *match;

                if (sum > ENVIRONMENT_BLOCK_MAX) /* Give up searching eventually */
                        return -ENOBUFS;

                r = read_nul_string(f, LONG_LINE_MAX, &line);
                if (r < 0)
                        return r;
                if (r == 0)  /* EOF */
                        break;

                sum += r;

                match = startswith(line, field);
                if (match && *match == '=')
                        return strdup_to_full(ret, match + 1);
        }

        *ret = NULL;
        return 0;
}

int pidref_is_unwaited(PidRef *pid) {
        int r;

        /* Checks whether a PID is still valid at all, including a zombie */

        if (!pidref_is_set(pid))
                return -ESRCH;

        if (pidref_is_remote(pid))
                return -EREMOTE;

        if (pid->pid == 1 || pidref_is_self(pid))
                return true;

        r = pidref_kill(pid, 0);
        if (r == -ESRCH)
                return false;
        if (r < 0)
                return r;

        return true;
}

int pid_is_unwaited(pid_t pid) {

        if (pid == 0)
                return true;

        return pidref_is_unwaited(&PIDREF_MAKE_FROM_PID(pid));
}

bool is_main_thread(void) {
        static thread_local int cached = -1;

        if (cached < 0)
                cached = getpid_cached() == gettid();

        return cached;
}

/* Wrappers around sched_get_priority_{min,max}() that gracefully handles missing SCHED_EXT support in the kernel */
/* The cached PID, possible values:
 *
 *     == UNSET [0]  → cache not initialized yet
 *     == BUSY [-1]  → some thread is initializing it at the moment
 *     any other     → the cached PID
 */

#define CACHED_PID_UNSET ((pid_t) 0)
#define CACHED_PID_BUSY ((pid_t) -1)

static pid_t cached_pid = CACHED_PID_UNSET;

void reset_cached_pid(void) {
        /* Invoked in the child after a fork(), i.e. at the first moment the PID changed */
        cached_pid = CACHED_PID_UNSET;
}

pid_t getpid_cached(void) {
        static bool installed = false;
        pid_t current_value = CACHED_PID_UNSET;

        /* getpid_cached() is much like getpid(), but caches the value in local memory, to avoid having to invoke a
         * system call each time. This restores glibc behaviour from before 2.24, when getpid() was unconditionally
         * cached. Starting with 2.24 getpid() started to become prohibitively expensive when used for detecting when
         * objects were used across fork()s. With this caching the old behaviour is somewhat restored.
         *
         * https://bugzilla.redhat.com/show_bug.cgi?id=1443976
         * https://sourceware.org/git/gitweb.cgi?p=glibc.git;h=c579f48edba88380635ab98cb612030e3ed8691e
         */

        (void) __atomic_compare_exchange_n(
                        &cached_pid,
                        &current_value,
                        CACHED_PID_BUSY,
                        false,
                        __ATOMIC_SEQ_CST,
                        __ATOMIC_SEQ_CST);

        switch (current_value) {

        case CACHED_PID_UNSET: { /* Not initialized yet, then do so now */
                pid_t new_pid;

                new_pid = getpid();

                if (!installed) {
                        /* __register_atfork() either returns 0 or -ENOMEM, in its glibc implementation. Since it's
                         * only half-documented (glibc doesn't document it but LSB does — though only superficially)
                         * we'll check for errors only in the most generic fashion possible. */

                        if (pthread_atfork(NULL, NULL, reset_cached_pid) != 0) {
                                /* OOM? Let's try again later */
                                cached_pid = CACHED_PID_UNSET;
                                return new_pid;
                        }

                        installed = true;
                }

                cached_pid = new_pid;
                return new_pid;
        }

        case CACHED_PID_BUSY: /* Somebody else is currently initializing */
                return getpid();

        default: /* Properly initialized */
                return current_value;
        }
}

static int fork_flags_to_signal(ForkFlags flags) {
        return (flags & FORK_DEATHSIG_SIGTERM) ? SIGTERM : SIGKILL;
}

int pidref_safe_fork_full(
                const char *name,
                const int stdio_fds[3],
                int except_fds[],
                size_t n_except_fds,
                ForkFlags flags,
                PidRef *ret) {

        pid_t original_pid, pid;
        sigset_t saved_ss, ss;
        _unused_ _cleanup_(block_signals_reset) sigset_t *saved_ssp = NULL;
        bool block_signals = false, block_all = false;
        int r;

        assert((flags & FORK_UNSUPPORTED) == 0);

        /* A wrapper around fork(), that does a couple of important initializations in addition to mere
         * forking. If provided, ret is initialized in both the parent and the child process, both times
         * referencing the child process. Returns == 0 in the child and > 0 in the parent. */

        original_pid = getpid_cached();

        if (flags & (FORK_RESET_SIGNALS|FORK_DEATHSIG_SIGTERM)) {
                /* We temporarily block all signals, so that the new child has them blocked initially. This
                 * way, we can be sure that SIGTERMs are not lost we might send to the child. (Note that for
                 * FORK_DEATHSIG_SIGKILL we don't bother, since it cannot be blocked anyway.) */

                assert_se(sigfillset(&ss) >= 0);
                block_signals = block_all = true;

        } else if (flags & FORK_WAIT) {
                /* Let's block SIGCHLD at least, so that we can safely watch for the child process */

                assert_se(sigemptyset(&ss) >= 0);
                assert_se(sigaddset(&ss, SIGCHLD) >= 0);
                block_signals = true;
        }

        if (block_signals) {
                if (sigprocmask(SIG_BLOCK, &ss, &saved_ss) < 0)
                        return log_debug_errno(errno, "Failed to block signal mask: %m");
                saved_ssp = &saved_ss;
        }

        pid = fork();
        if (pid < 0)
                return log_debug_errno(errno, "Failed to fork off '%s': %m", strna(name));
        if (pid > 0) {

                /* We are in the parent process */
                log_debug("Successfully forked off '%s' as PID " PID_FMT ".", strna(name), pid);

                if (flags & FORK_WAIT) {
                        if (block_all) {
                                /* undo everything except SIGCHLD */
                                ss = saved_ss;
                                assert_se(sigaddset(&ss, SIGCHLD) >= 0);
                                (void) sigprocmask(SIG_SETMASK, &ss, NULL);
                        }

                        r = pidref_wait_for_terminate_and_check(
                                        name,
                                        &PIDREF_MAKE_FROM_PID(pid),
                                        FLAGS_SET(flags, FORK_LOG) ? WAIT_LOG : 0);
                        if (r < 0)
                                return r;
                        if (r != EXIT_SUCCESS) /* exit status > 0 should be treated as failure, too */
                                return -EPROTO;

                        /* If we are in the parent and successfully waited, then the process doesn't exist anymore. */
                        if (ret)
                                *ret = PIDREF_NULL;

                        return 1;
                }

                if (ret) {
                        r = pidref_set_pid(ret, pid);
                        if (r < 0) /* Let's not fail for this, no matter what, the process exists after all, and that's key */
                                *ret = PIDREF_MAKE_FROM_PID(pid);
                }

                return 1;
        }

        /* We are in the child process */

        /* Restore signal mask manually */
        saved_ssp = NULL;

        if (flags & FORK_REOPEN_LOG) {
                /* Close the logs if requested, before we log anything. And make sure we reopen it if needed. */
                log_close();
                log_set_open_when_needed(true);
                log_settle_target();
        }

        if (name) {
                r = rename_process(name);
                if (r < 0)
                        log_debug_errno(r, "Failed to rename process, ignoring: %m");
        }

        /* let's disable dlopen() in the child, as a paranoia safety precaution: children should not live for
         * long and only do minimal work before exiting or exec()ing. Doing dlopen() is not either. If people
         * want dlopen() they should do it before forking. This is a safety precaution in particular for
         * cases where the child does namespace shenanigans: we should never end up loading a module from a
         * foreign environment. Note that this has no effect on NSS! (i.e. it only has effect on uses of our
         * dlopen_safe(), which we use comprehensively in our codebase, but glibc NSS doesn't bother, of
         * course.) */
        block_dlopen();

        if (flags & (FORK_DEATHSIG_SIGTERM|FORK_DEATHSIG_SIGKILL)) {
                r = prctl_safe(PR_SET_PDEATHSIG, fork_flags_to_signal(flags), 0, 0, 0);
                if (r < 0) {
                        log_debug_errno(r, "Failed to set death signal: %m");
                        _exit(EXIT_FAILURE);
                }
        }

        if (flags & FORK_RESET_SIGNALS) {
                r = reset_all_signal_handlers();
                if (r < 0) {
                        log_debug_errno(r, "Failed to reset signal handlers: %m");
                        _exit(EXIT_FAILURE);
                }

                /* This implicitly undoes the signal mask stuff we did before the fork()ing above */
                r = reset_signal_mask();
                if (r < 0) {
                        log_debug_errno(r, "Failed to reset signal mask: %m");
                        _exit(EXIT_FAILURE);
                }
        } else if (block_signals) { /* undo what we did above */
                if (sigprocmask(SIG_SETMASK, &saved_ss, NULL) < 0) {
                        log_debug_errno(errno, "Failed to restore signal mask: %m");
                        _exit(EXIT_FAILURE);
                }
        }

        if (flags & (FORK_DEATHSIG_SIGTERM|FORK_DEATHSIG_SIGKILL)) {
                pid_t ppid;
                /* Let's see if the parent PID is still the one we started from? If not, then the parent
                 * already died by the time we set PR_SET_PDEATHSIG, hence let's emulate the effect */

                ppid = getppid();
                if (ppid == 0)
                        /* Parent is in a different PID namespace. */;
                else if (ppid != original_pid) {
                        int sig = fork_flags_to_signal(flags);
                        log_debug("Parent died early, raising %s.", signal_to_string(sig));
                        (void) raise(sig);
                        _exit(EXIT_FAILURE);
                }
        }

        if (flags & FORK_REARRANGE_STDIO) {
                if (stdio_fds) {
                        r = rearrange_stdio(stdio_fds[0], stdio_fds[1], stdio_fds[2]);
                        if (r < 0) {
                                log_debug_errno(r, "Failed to rearrange stdio fds: %m");
                                _exit(EXIT_FAILURE);
                        }

                        /* Turn off O_NONBLOCK on the fdio fds, in case it was left on */
                        stdio_disable_nonblock();
                } else {
                        r = make_null_stdio();
                        if (r < 0) {
                                log_debug_errno(r, "Failed to connect stdin/stdout to /dev/null: %m");
                                _exit(EXIT_FAILURE);
                        }
                }
        }

        if (flags & FORK_CLOSE_ALL_FDS) {
                /* Close the logs here in case it got reopened above, as close_all_fds() would close them for us */
                log_close();

                r = close_all_fds(except_fds, n_except_fds);
                if (r < 0) {
                        log_debug_errno(r, "Failed to close all file descriptors: %m");
                        _exit(EXIT_FAILURE);
                }
        }

        if (flags & FORK_PACK_FDS) {
                /* FORK_CLOSE_ALL_FDS ensures that except_fds are the only FDs >= 3 that are
                 * open, this is including the log. This is required by pack_fds, which will
                 * get stuck in an infinite loop of any FDs other than except_fds are open. */
                assert(FLAGS_SET(flags, FORK_CLOSE_ALL_FDS));

                r = pack_fds(except_fds, n_except_fds);
                if (r < 0) {
                        log_debug_errno(r, "Failed to pack file descriptors: %m");
                        _exit(EXIT_FAILURE);
                }
        }

        if (flags & FORK_CLOEXEC_OFF) {
                r = fd_cloexec_many(except_fds, n_except_fds, false);
                if (r < 0) {
                        log_debug_errno(r, "Failed to turn off O_CLOEXEC on file descriptors: %m");
                        _exit(EXIT_FAILURE);
                }
        }

        /* When we were asked to reopen the logs, do so again now */
        if (flags & FORK_REOPEN_LOG) {
                log_open();
                log_set_open_when_needed(false);
        }

        if (flags & FORK_RLIMIT_NOFILE_SAFE) {
                r = rlimit_nofile_safe();
                if (r < 0) {
                        log_debug_errno(r, "Failed to lower RLIMIT_NOFILE's soft limit to 1K: %m");
                        _exit(EXIT_FAILURE);
                }
        }

        r = RET_NERRNO(unsetenv("NOTIFY_SOCKET"));
        if (r < 0) {
                log_debug_errno(r, "Failed to unset $NOTIFY_SOCKET: %m");
                _exit(EXIT_FAILURE);
        }

        if (ret) {
                r = pidref_set_self(ret);
                if (r < 0) {
                        log_debug_errno(r, "Failed to acquire PID reference on ourselves: %m");
                        _exit(EXIT_FAILURE);
                }
        }

        return 0;
}

int namespace_fork_full(
                const char *outer_name,
                const char *inner_name,
                int except_fds[],
                size_t n_except_fds,
                ForkFlags flags,
                int pidns_fd,
                int mntns_fd,
                int netns_fd,
                int userns_fd,
                int root_fd,
                PidRef *ret) {

        _cleanup_(pidref_done_sigkill_wait) PidRef pidref_outer = PIDREF_NULL;
        _cleanup_close_pair_ int errno_pipe_fd[2] = EBADF_PAIR;
        int r;

        /* This is much like safe_fork(), but forks twice, and joins the specified namespaces in the middle
         * process. This ensures that we are fully a member of the destination namespace, with pidns an all, so that
         * /proc/self/fd works correctly.
         *
         * TODO: once we can rely on PIDFD_INFO_EXIT, do not keep the middle process around and instead
         * return the pidfd of the inner process for direct tracking. */

        /* Insist on PDEATHSIG being enabled, as the pid returned is the one of the middle man, and otherwise
         * killing of it won't be propagated to the inner child. */
        assert((flags & FORK_UNSUPPORTED) == 0);
        assert((flags & (FORK_DEATHSIG_SIGKILL|FORK_DEATHSIG_SIGTERM)) != 0);

        /* We want read() to block as a synchronization point */
        assert_cc(sizeof(int) <= PIPE_BUF);
        if (pipe2(errno_pipe_fd, O_CLOEXEC) < 0)
                return log_debug_errno(errno, "Failed to create pipe: %m");

        r = pidref_safe_fork_full(
                        outer_name,
                        /* stdio_fds= */ NULL, /* except_fds= */ NULL, /* n_except_fds= */ 0,
                        (flags|FORK_DEATHSIG_SIGKILL) & ~(FORK_DEATHSIG_SIGTERM|FORK_DEATHSIG_SIGINT|FORK_REOPEN_LOG|FORK_NEW_MOUNTNS|FORK_MOUNTNS_SLAVE|FORK_NEW_USERNS|FORK_NEW_NETNS|FORK_NEW_PIDNS|FORK_CLOSE_ALL_FDS|FORK_PACK_FDS|FORK_CLOEXEC_OFF|FORK_RLIMIT_NOFILE_SAFE),
                        &pidref_outer);
        if (r == -EPROTO && FLAGS_SET(flags, FORK_WAIT)) {
                errno_pipe_fd[1] = safe_close(errno_pipe_fd[1]);

                int k = read_errno(errno_pipe_fd[0]);
                if (k < 0 && k != -EIO)
                        return k;
        }
        if (r < 0)
                return r;
        if (r == 0) {
                _cleanup_(pidref_done) PidRef pidref_inner = PIDREF_NULL;

                /* Child */

                errno_pipe_fd[0] = safe_close(errno_pipe_fd[0]);

                r = namespace_enter(pidns_fd, mntns_fd, netns_fd, userns_fd, root_fd);
                if (r < 0) {
                        log_debug_errno(r, "Failed to join namespace: %m");
                        report_errno_and_exit(errno_pipe_fd[1], r);
                }

                /* We mask a few flags here that either make no sense for the grandchild, or that we don't have to do again */
                r = pidref_safe_fork_full(
                                inner_name,
                                NULL,
                                except_fds, n_except_fds,
                                flags & ~(FORK_WAIT|FORK_RESET_SIGNALS|FORK_REARRANGE_STDIO|FORK_FLUSH_STDIO|FORK_STDOUT_TO_STDERR),
                                &pidref_inner);
                if (r < 0)
                        report_errno_and_exit(errno_pipe_fd[1], r);
                if (r == 0) {
                        /* Child */

                        if (!FLAGS_SET(flags, FORK_CLOSE_ALL_FDS)) {
                                errno_pipe_fd[1] = safe_close(errno_pipe_fd[1]);
                                pidref_done(&pidref_outer);
                        } else {
                                errno_pipe_fd[1] = -EBADF;
                                pidref_outer = PIDREF_NULL;
                        }

                        if (ret)
                                *ret = TAKE_PIDREF(pidref_inner);
                        return 0;
                }

                log_forget_fds();
                log_set_open_when_needed(true);

                (void) close_all_fds(&pidref_inner.fd, 1);

                r = pidref_wait_for_terminate_and_check(
                                inner_name,
                                &pidref_inner,
                                FLAGS_SET(flags, FORK_LOG) ? WAIT_LOG : 0);
                if (r < 0)
                        _exit(EXIT_FAILURE);

                _exit(r);
        }

        errno_pipe_fd[1] = safe_close(errno_pipe_fd[1]);

        r = read_errno(errno_pipe_fd[0]);
        if (r < 0)
                return r; /* the child logs about failures on its own, no need to duplicate here */

        if (ret)
                *ret = TAKE_PIDREF(pidref_outer);
        else
                pidref_done(&pidref_outer); /* disarm sigkill_wait */

        return 1;
}


int get_process_threads(pid_t pid) {
        _cleanup_free_ char *t = NULL;
        int n, r;

        if (pid < 0)
                return -EINVAL;

        r = procfs_file_get_field(pid, "status", "Threads", &t);
        if (r == -ENOENT)
                return -ESRCH;
        if (r < 0)
                return r;

        r = safe_atoi(t, &n);
        if (r < 0)
                return r;
        if (n < 0)
                return -EINVAL;

        return n;
}


_noreturn_ void report_errno_and_exit(int errno_fd, int error) {
        int r;

        if (error >= 0)
                _exit(EXIT_SUCCESS);

        assert(errno_fd >= 0);

        r = loop_write(errno_fd, &error, sizeof(error));
        if (r < 0)
                log_debug_errno(r, "Failed to write errno to errno_fd=%d: %m", errno_fd);

        _exit(EXIT_FAILURE);
}

int read_errno(int errno_fd) {
        int r;

        assert(errno_fd >= 0);

        /* The issue here is that it's impossible to distinguish between an error code returned by child and
         * IO error arose when reading it. So, the function logs errors and return EIO for the later case. */

        ssize_t n = loop_read(errno_fd, &r, sizeof(r), /* do_poll= */ false);
        if (n < 0) {
                log_debug_errno(n, "Failed to read errno: %m");
                return -EIO;
        }
        if (n == 0) /* the process exited without reporting an error, assuming success */
                return 0;
        if (n != sizeof(r))
                return log_debug_errno(SYNTHETIC_ERRNO(EIO), "Received unexpected amount of bytes (%zi) while reading errno.", n);

        if (r == 0)
                return 0;
        if (r < 0) /* child process reported an error, return it */
                return log_debug_errno(r, "Child process failed with errno: %m");

        return log_debug_errno(SYNTHETIC_ERRNO(EIO), "Received positive errno from child, refusing: %d", r);
}

int prctl_safe(int op, unsigned long arg2, unsigned long arg3, unsigned long arg4, unsigned long arg5) {

        /* prctl(2) is a bit messy: it's a variadic function, defined with "unsigned long" arguments. This
         * means that unless people explicitly cast it's quite likely they end up passing a shorter type even
         * though unsigned long is required. And most of the time it might even kind of work, but not
         * always. Moreover, some calls insist on all unused arguments being zeroed out, others don't
         * care. Let's define this wrapper to enforce the right types, and that all arguments are always
         * passed, to avoid this confusion. */

        return RET_NERRNO(prctl(op, arg2, arg3, arg4, arg5));
}

int proc_set_comm(const char *comm) {
        return prctl_safe(PR_SET_NAME, (unsigned long) comm, 0, 0, 0);
}

