/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "chase.h"
#include "fd-util.h"
#include "fs-util.h"
#include "log.h"
#include "path-util.h"
#include "stat-util.h"
#include "string-util.h"
#include "strv.h"

static int chase_statx(int fd, struct statx *ret) {
        return xstatx_full(fd,
                        /* path= */ NULL,
                        /* statx_flags= */ 0,
                        XSTATX_MNT_ID_BEST,
                        STATX_TYPE|STATX_UID|STATX_INO,
                        /* optional_mask= */ 0,
                        /* mandatory_attributes= */ 0,
                        ret);
}

static int chase_openat2(int root_fd, int dir_fd, const char *path, ChaseFlags chase_flags) {
        /* Open the target of a chase operation via openat2(), translating the relevant ChaseFlags into
         * RESOLVE_* and O_* flags and verifying MUST_BE_REGULAR/SOCKET via fstat after the open. Returns
         * -EOPNOTSUPP when openat2() is unavailable (older kernels) or blocked by a seccomp filter
         * (notably systemd's own filter, which returns ENOSYS to force programs onto the openat()
         * fallback path) — the verdict is cached so subsequent calls in the same process skip the syscall
         * entirely. */

        static bool can_openat2 = true;

        assert(path);
        assert(wildcard_fd_is_valid(dir_fd));
        assert((chase_flags & CHASE_UNSUPPORTED) == 0);

        if (!can_openat2)
                return -EOPNOTSUPP;

        /* RESOLVE_IN_ROOT requires the dirfd to be the root. Bail out so the caller falls back to the
         * regular chase loop. */
        if (root_fd != XAT_FDROOT && root_fd != dir_fd)
                return -EOPNOTSUPP;

        _cleanup_close_ int dir_fd_local = -EBADF;
        if (dir_fd == XAT_FDROOT) {
                if (path_is_absolute(path))
                        dir_fd = AT_FDCWD;
                else {
                        dir_fd_local = open("/", O_CLOEXEC|O_DIRECTORY|O_PATH);
                        if (dir_fd_local < 0)
                                return -errno;
                        dir_fd = dir_fd_local;
                }
        }

        struct open_how how = {
                .flags = O_PATH|O_CLOEXEC,
        };
        if (FLAGS_SET(chase_flags, CHASE_MUST_BE_DIRECTORY))
                how.flags |= O_DIRECTORY;
        if (root_fd != XAT_FDROOT)
                how.resolve |= RESOLVE_IN_ROOT;

        _cleanup_close_ int fd = openat2(dir_fd, path, &how, sizeof(how));
        if (fd < 0) {
                /* ENOSYS: kernel too old or seccomp filter (systemd's filter returns ENOSYS).
                 * EPERM: Some seccomp profiles of container runtimes use EPERM rather than ENOSYS.
                 * But EPERM might also be returned because we can't access some component of the path. So
                 * we can't cache the result and skip using openat2() if it is blocked with EPERM. Instead
                 * we fall back to userspace chase() if we get EPERM.
                 * EAGAIN: with RESOLVE_IN_ROOT the kernel returns this when a ".." component
                 * (typically from following a symlink like /etc/os-release → ../usr/lib/os-release)
                 * is processed and the global mount_lock or rename_lock seqcount changed during
                 * the walk. Any mount activity anywhere in the system bumps mount_lock, so this
                 * fires reliably while we're still setting up a mount tree. Fall back to the
                 * regular chase loop, which handles root boundaries without openat2(). Don't
                 * cache this — the condition is per-call, not a kernel/sandbox capability. */
                if (errno == ENOSYS)
                        can_openat2 = false;
                if (IN_SET(errno, ENOSYS, EPERM, EAGAIN))
                        return -EOPNOTSUPP;
                return -errno;
        }

        return TAKE_FD(fd);
}

static int chase_xopenat(int dir_fd, const char *path, ChaseFlags chase_flags, int open_flags, XOpenFlags xopen_flags) {
        /* Wrapper around xopenat_full() that translates CHASE_MUST_BE_DIRECTORY into its xopenat_full()
         * counterpart. Used by shortcuts that want to open the final target of a chase operation. */

        assert((chase_flags & CHASE_UNSUPPORTED) == 0);

        if (FLAGS_SET(chase_flags, CHASE_MUST_BE_DIRECTORY))
                open_flags |= O_DIRECTORY;

        return xopenat_full(dir_fd, path, open_flags, xopen_flags, MODE_INVALID);
}

int chaseat(int root_fd, int dir_fd, const char *path, ChaseFlags flags, char **ret_path, int *ret_fd) {
        int r;

        assert((flags & CHASE_UNSUPPORTED) == 0);
        assert(wildcard_fd_is_valid(dir_fd));
        assert(wildcard_fd_is_valid(root_fd));
        /* AT_FDCWD for dir_fd is only allowed when there is no chroot boundary: otherwise the current
         * working directory might live outside root_fd's subtree. */
        assert(dir_fd != AT_FDCWD || IN_SET(root_fd, AT_FDCWD, XAT_FDROOT));

        /* This function resolves symlinks of the path relative to the given directory file descriptor.
         * The root directory file descriptor sets the chroot boundary: symlinks may not escape it, and
         * absolute symlinks encountered during resolution are resolved relative to it. When the root fd is
         * XAT_FDROOT, symlinks are resolved relative to the host's root directory with no containment.
         *
         * The given path is always resolved starting at dir_fd, regardless of whether it is absolute or
         * relative. The leading slashes of an absolute path are ignored. The only exceptions are
         * dir_fd == XAT_FDROOT (which starts resolution at root_fd) and dir_fd == AT_FDCWD with an absolute
         * path (which starts resolution at "/" rather than the current working directory).
         *
         * Note that we do not verify that dir_fd actually points to a descendant of root_fd. If dir_fd
         * lies outside the root_fd subtree, ".." traversal and absolute symlinks may still be clamped to
         * root_fd, leading to surprising results. Callers must ensure the relationship themselves.
         *
         * Absolute paths returned by this function are relative to the given root file descriptor. Relative
         * paths returned by this function are relative to the given directory file descriptor. The result is
         * absolute when root_fd is XAT_FDROOT (i.e. there is no chroot boundary, so openat()-like callers
         * need an absolute path to reach the host inode), or when an absolute symlink made us jump to a
         * different subtree than the one dir_fd points into. Otherwise the result is relative.
         *
         * Algorithmically this operates on two path buffers: "done" are the components of the path we
         * already processed and resolved symlinks, "." and ".." of. "todo" are the components of the path we
         * still need to process. On each iteration, we move one component from "todo" to "done", processing
         * its special meaning each time. We always keep an O_PATH fd to the component we are currently
         * processing, thus keeping lookup races to a minimum.
         *
         * There are two ways to invoke this function:
         *
         * 1. With ret_path: the path is resolved and the normalized path is returned in `ret_path`. The
         *    return value is < 0 on error, and 1 if the destination was found. -ENOENT is returned if it
         *    wasn't.
         *
         * 2. With ret_fd: in this case the destination is opened after chasing it as O_PATH and this file
         *    descriptor is returned as return value. This is useful to open files relative to some root
         *    directory. Note that the returned O_PATH file descriptors must be converted into a regular one
         *    (using fd_reopen() or such) before it can be used for reading/writing.
         */

        /* We treat AT_FDCWD as XAT_FDROOT for a more seamless migration for all callers of chaseat() before
         * it was reworked to support separate root_fd and dir_fd arguments. */
        if (root_fd == AT_FDCWD)
                root_fd = XAT_FDROOT;
        else {
                r = dir_fd_is_root(root_fd);
                if (r < 0)
                        return r;
                if (r > 0)
                        root_fd = XAT_FDROOT;
        }

        /* If dir_fd points to the host's root directory and there is no chroot boundary, normalize it
         * to XAT_FDROOT so the shortcut path can kick in. */
        r = dir_fd_is_root(dir_fd);
        if (r < 0)
                return r;
        if (r > 0 && root_fd == XAT_FDROOT)
                dir_fd = XAT_FDROOT;

        /* dir_fd == XAT_FDROOT means "start at root_fd". An absolute path is always resolved relative to
         * root_fd, regardless of what dir_fd points to. */
        if (dir_fd == XAT_FDROOT || path_is_absolute(path))
                dir_fd = root_fd;

        if (isempty(path))
                path = ".";

        if (ENDSWITH_SET(path, "/", "/.") || dot_or_dot_dot(path) || endswith(path, "/.."))
                flags |= CHASE_MUST_BE_DIRECTORY;

        if (!ret_path) {
                r = chase_openat2(root_fd, dir_fd, path, flags);
                if (r >= 0) {
                        if (ret_fd)
                                *ret_fd = r;
                        else
                                safe_close(r);

                        return 1;
                }
                if (r != -EOPNOTSUPP)
                        return r;
        }

        if (root_fd == XAT_FDROOT && !ret_path) {
                /* Shortcut the common case where we don't have a real root boundary: open the target
                 * directly via xopenat_full(). */

                r = chase_xopenat(dir_fd, path, flags, O_PATH|O_CLOEXEC, /* xopen_flags= */ 0);
                if (r < 0)
                        return r;

                if (ret_fd)
                        *ret_fd = r;
                else
                        safe_close(r);

                return 1;
        }

        /* Decide whether to return an absolute or relative path.
         *
         * We return an absolute path only when there is no chroot boundary (root_fd == XAT_FDROOT)
         * and resolution starts from root — i.e. either dir_fd was XAT_FDROOT or path is absolute,
         * both of which caused dir_fd = root_fd above. In every other case we return a relative
         * path so the result keeps working when fed to an openat()-style call against dir_fd,
         * which would ignore dir_fd if handed an absolute path.
         *
         * When root_fd != XAT_FDROOT and an absolute symlink later causes resolution to escape
         * dir_fd, the loop below rebases onto root_fd and switches to an absolute result at that
         * point — it is not handled here.
         */
        bool need_absolute = (root_fd == XAT_FDROOT || dir_fd != root_fd) && (dir_fd == XAT_FDROOT || path_is_absolute(path));

        _cleanup_free_ char *done = NULL;
        if (need_absolute) {
                done = strdup("/");
                if (!done)
                        return -ENOMEM;
        }

        _cleanup_close_ int fd = xopenat(dir_fd, NULL, O_CLOEXEC|O_DIRECTORY|O_PATH);
        if (fd < 0)
                return fd;

        struct statx stx;
        r = chase_statx(fd, &stx);
        if (r < 0)
                return r;

        /* Remember stat data of the root, so that we can recognize it later during .. handling. Only
         * needed when there is an actual chroot boundary — with root_fd == XAT_FDROOT the boundary
         * check in the .. loop below is skipped and root_stx is never consulted. */
        struct statx root_stx;
        if (root_fd != XAT_FDROOT) {
                if (root_fd == dir_fd)
                        root_stx = stx;
                else {
                        r = chase_statx(root_fd, &root_stx);
                        if (r < 0)
                                return r;
                }
        }

        _cleanup_free_ char *buffer = strdup(path);
        if (!buffer)
                return -ENOMEM;

        const char *todo = buffer;
        for (unsigned n_steps = 0;; n_steps++) {
                _cleanup_free_ char *first = NULL;
                _cleanup_close_ int child = -EBADF;
                struct statx stx_child;
                const char *e;

                /* If people change our tree behind our back, they might send us in circles. Put a limit on
                 * things */
                if (n_steps > CHASE_MAX)
                        return -ELOOP;

                r = path_find_first_component(&todo, /* accept_dot_dot= */ true, &e);
                if (r < 0)
                        return r;
                if (r == 0) /* We reached the end. */
                        break;

                first = strndup(e, r);
                if (!first)
                        return -ENOMEM;

                /* Two dots? Then chop off the last bit of what we already found out. */
                if (streq(first, "..")) {
                        _cleanup_free_ char *parent = NULL;
                        _cleanup_close_ int fd_parent = -EBADF;
                        struct statx stx_parent;

                        /* If we already are at the top, then going up will not change anything. This is
                         * in-line with how the kernel handles this. We check this both by path and by
                         * inode/mount identity check. The latter is load-bearing if concurrent access of the
                         * root tree we operate in is allowed, where an inode is moved up the tree while we
                         * look at it, and thus get the current path wrong and think we are deeper down than
                         * we actually are.
                         *
                         * The path-based fast path is only valid when the caller started at the root fd:
                         * otherwise 'done' being empty just means we haven't descended past the starting
                         * dir_fd, not that we're at the chroot boundary. */
                        if (root_fd != XAT_FDROOT) {
                                bool is_root = root_fd == dir_fd && empty_or_root(done);
                                if (!is_root && statx_inode_same(&stx, &root_stx)) {
                                        r = statx_mount_same(&stx, &root_stx);
                                        if (r < 0)
                                                return r;

                                        is_root = r > 0;
                                }
                                if (is_root)
                                        continue;
                        }

                        fd_parent = openat(fd, "..", O_CLOEXEC|O_NOFOLLOW|O_PATH|O_DIRECTORY);
                        if (fd_parent < 0)
                                return -errno;

                        r = chase_statx(fd_parent, &stx_parent);
                        if (r < 0)
                                return r;

                        /* If we opened the same directory, that _may_ indicate that we're at the host root
                         * directory. Let's confirm that in more detail with dir_fd_is_root(). And if so,
                         * going up won't change anything. */
                        if (statx_inode_same(&stx_parent, &stx)) {
                                r = dir_fd_is_root(fd);
                                if (r < 0)
                                        return r;
                                if (r > 0)
                                        continue;
                        }

                        r = path_extract_directory(done, &parent);
                        if (r >= 0) {
                                assert(!need_absolute || path_is_absolute(parent));
                                free_and_replace(done, parent);
                        } else if (r == -EDESTADDRREQ) {
                                /* 'done' contains filename only (i.e. no slash). */
                                assert(!need_absolute);
                                done = mfree(done);
                        } else if (r == -EADDRNOTAVAIL) {
                                /* 'done' is "/". This branch should already be handled above via the
                                 * is_root check. */
                                assert_not_reached();
                        } else if (r == -EINVAL) {
                                /* 'done' is empty (we haven't descended past the starting dir_fd yet), or
                                 * ends with '..'. In both cases we're traversing above the starting point
                                 * (valid when root_fd is XAT_FDROOT, or when dir_fd was below root_fd to
                                 * start with), so record another '..' in 'done'. */
                                assert(!need_absolute);

                                if (!isempty(done) && !path_is_valid(done))
                                        return -EINVAL;

                                if (!path_extend(&done, ".."))
                                        return -ENOMEM;
                        } else
                                return r;

                        /* update fd and stat */
                        stx = stx_parent;
                        close_and_replace(fd, fd_parent);
                        continue;
                }

                /* Otherwise let's pin it by file descriptor, via O_PATH. */
                child = r = xopenat_full(fd, first,
                                         O_PATH|O_NOFOLLOW|O_CLOEXEC,
                                         /* xopen_flags= */ 0,
                                         MODE_INVALID);
                if (r < 0)
                        return r;

                r = chase_statx(child, &stx_child);
                if (r < 0)
                        return r;

                if (S_ISLNK(stx_child.stx_mode)) {
                        _cleanup_free_ char *destination = NULL;

                        r = readlinkat_malloc(fd, first, &destination);
                        if (r < 0)
                                return r;
                        if (isempty(destination))
                                return -EINVAL;

                        if (path_is_absolute(destination)) {

                                /* An absolute destination. Start the loop from the beginning, but use the
                                 * root file descriptor as base. */

                                safe_close(fd);
                                fd = fd_reopen(root_fd, O_CLOEXEC|O_PATH|O_DIRECTORY);
                                if (fd < 0)
                                        return fd;

                                r = chase_statx(fd, &stx);
                                if (r < 0)
                                        return r;

                                if (dir_fd != root_fd)
                                        need_absolute = true;

                                r = free_and_strdup(&done, need_absolute ? "/" : NULL);
                                if (r < 0)
                                        return r;
                        }

                        /* Prefix what's left to do with what we just read, and start the loop again, but
                         * remain in the current directory. */
                        if (!path_extend(&destination, todo))
                                return -ENOMEM;

                        free_and_replace(buffer, destination);
                        todo = buffer;

                        continue;
                }

                /* If this is not a symlink, then let's just add the name we read to what we already verified. */
                if (!path_extend(&done, first))
                        return -ENOMEM;

                /* And iterate again, but go one directory further down. */
                stx = stx_child;
                close_and_replace(fd, child);
        }

        if (FLAGS_SET(flags, CHASE_MUST_BE_DIRECTORY)) {
                r = statx_verify_directory(&stx);
                if (r < 0)
                        return r;
        }

        if (ret_path) {
                if (!done) {
                        assert(!need_absolute);
                        done = strdup(".");
                        if (!done)
                                return -ENOMEM;
                }

                *ret_path = TAKE_PTR(done);
        }

        if (ret_fd) {
                /* Return the O_PATH fd we currently are looking to the caller. It can translate it to a
                 * proper fd by opening /proc/self/fd/xyz. */
                assert(fd >= 0);
                *ret_fd = TAKE_FD(fd);
        }

        return 1;
}

int chase(const char *path, const char *root, ChaseFlags flags, char **ret_path, int *ret_fd) {
        _cleanup_free_ char *root_abs = NULL, *absolute = NULL, *p = NULL;
        _cleanup_close_ int fd = -EBADF, pfd = -EBADF;
        int r;

        assert(path);
        assert((flags & CHASE_UNSUPPORTED) == 0);

        if (isempty(path))
                return -EINVAL;

        r = empty_or_root_harder_to_null(&root);
        if (r < 0)
                return r;

        /* A root directory of "/" or "" is identical to "/". */
        if (empty_or_root(root))
                root = "/";
        else {
                r = path_make_absolute_cwd(root, &root_abs);
                if (r < 0)
                        return r;

                /* Simplify the root directory, so that it has no duplicate slashes and nothing at the
                 * end. While we won't resolve the root path we still simplify it. */
                root = path_simplify(root_abs);

                assert(path_is_absolute(root));
                assert(!empty_or_root(root));
        }

        r = path_make_absolute_cwd(path, &absolute);
        if (r < 0)
                return r;

        path = path_startswith(absolute, root);
        if (!path)
                return log_debug_errno(SYNTHETIC_ERRNO(ECHRNG),
                                       "Specified path '%s' is outside of specified root directory '%s', refusing to resolve.",
                                       absolute, root);

        if (empty_or_root(root))
                fd = XAT_FDROOT;
        else {
                fd = open(root, O_CLOEXEC|O_DIRECTORY|O_PATH);
                if (fd < 0)
                        return -errno;
        }

        r = chaseat(fd, fd, path, flags & ~CHASE_PREFIX_ROOT, ret_path ? &p : NULL, ret_fd ? &pfd : NULL);
        if (r < 0)
                return r;

        if (ret_path) {
                /* When "root" points to the root directory, the result of chaseat() is always absolute,
                 * hence it is not necessary to prefix with the root. When "root" points to a non-root
                 * directory, the result path is always normalized and relative, hence we can simply call
                 * path_join() and not necessary to call path_simplify(). As a special case, chaseat() may
                 * return "." or "./", which are normalized too, but we need to drop "." before merging with
                 * root. */

                if (empty_or_root(root))
                        assert(path_is_absolute(p));
                else {
                        char *q;

                        assert(!path_is_absolute(p));

                        q = path_join(root, p + STR_IN_SET(p, ".", "./"));
                        if (!q)
                                return -ENOMEM;

                        free_and_replace(p, q);
                }

                *ret_path = TAKE_PTR(p);
        }

        if (ret_fd)
                *ret_fd = TAKE_FD(pfd);

        return r;
}

