/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <sys/xattr.h>

#include "errno-util.h"
#include "fd-util.h"
#include "fs-util.h"
#include "string-util.h"

/* Use a single cache for all of *xattrat syscalls (added in kernel 6.13) */
static thread_local bool have_xattrat = true;

static int normalize_and_maybe_pin_inode(
                int *fd,
                const char **path,
                int *at_flags,
                int *ret_tfd,
                bool *ret_opath) {

        int r;

        assert(fd);
        assert(*fd >= 0 || *fd == AT_FDCWD);
        assert(path);
        assert(at_flags);
        assert(ret_tfd);
        assert(ret_opath);

        *path = empty_to_null(*path); /* Normalize "" to NULL */

        if (*fd == AT_FDCWD) {
                if (!*path) /* Both unspecified? Then operate on current working directory */
                        *path = ".";

                *ret_tfd = -EBADF;
                *ret_opath = false;
                return 0;
        }

        *at_flags |= AT_EMPTY_PATH;

        if (!*path) {
                r = fd_is_opath(*fd);
                if (r < 0)
                        return r;
                *ret_opath = r;
                *ret_tfd = -EBADF;
                return 0;
        }

        /* If both have been specified, then we go via O_PATH */

        int tfd = openat(*fd, *path, O_PATH|O_CLOEXEC|(FLAGS_SET(*at_flags, AT_SYMLINK_FOLLOW) ? 0 : O_NOFOLLOW));
        if (tfd < 0)
                return -errno;

        *fd = *ret_tfd = tfd;
        *path = NULL;
        *ret_opath = true;

        return 0;
}

static ssize_t getxattr_pinned_internal(
                int fd,
                const char *path,
                int at_flags,
                bool by_procfs,
                const char *name,
                char *buf,
                size_t size) {

        ssize_t n;

        assert(!path || !isempty(path));
        assert((fd >= 0) == !path);
        assert(path || FLAGS_SET(at_flags, AT_EMPTY_PATH));
        assert(name);
        assert(buf || size == 0);

        if (path)
                n = FLAGS_SET(at_flags, AT_SYMLINK_FOLLOW) ? getxattr(path, name, buf, size)
                                                           : lgetxattr(path, name, buf, size);
        else
                n = by_procfs ? getxattr(FORMAT_PROC_FD_PATH(fd), name, buf, size)
                              : fgetxattr(fd, name, buf, size);
        if (n < 0)
                return -errno;

        assert(size == 0 || (size_t) n <= size);
        return n;
}

int getxattr_at_malloc(
                int fd,
                const char *path,
                const char *name,
                int at_flags,
                char **ret,
                size_t *ret_size) {

        _cleanup_close_ int opened_fd = -EBADF;
        bool by_procfs;
        int r;

        assert(fd >= 0 || fd == AT_FDCWD);
        assert(name);
        assert((at_flags & ~(AT_SYMLINK_FOLLOW|AT_EMPTY_PATH)) == 0);
        assert(ret);

        /* So, this is single function that does what getxattr()/lgetxattr()/fgetxattr() does, but in one go,
         * and with additional bells and whistles. Specifically:
         *
         * 1. This works on O_PATH fds (via /proc/self/fd/, since getxattrat() syscall refuses them...)
         * 2. As extension to openat()-style semantics implies AT_EMPTY_PATH if path is empty
         * 3. Does a malloc() loop, automatically sizing the allocation
         * 4. NUL-terminates the returned buffer (for safety)
         */

        r = normalize_and_maybe_pin_inode(&fd, &path, &at_flags, &opened_fd, &by_procfs);
        if (r < 0)
                return r;

        size_t l = 100;
        for (unsigned n_attempts = 7;;) {
                _cleanup_free_ char *v = NULL;

                if (n_attempts == 0) /* If someone is racing against us, give up eventually */
                        return -EBUSY;
                n_attempts--;

                v = new(char, l+1);
                if (!v)
                        return -ENOMEM;

                l = MALLOC_ELEMENTSOF(v) - 1;

                ssize_t n;
                n = getxattr_pinned_internal(fd, path, at_flags, by_procfs, name, v, l);
                if (n >= 0) {
                        /* Refuse extended attributes with embedded NUL bytes if the caller isn't interested
                         * in the size. After all this must mean the caller assumes we return a NUL
                         * terminated strings, but if there's a NUL byte embedded they are definitely not
                         * regular strings */
                        if (!ret_size && n > 1 && memchr(v, 0, n - 1))
                                return -EBADMSG;

                        v[n] = 0; /* NUL terminate */
                        *ret = TAKE_PTR(v);
                        if (ret_size)
                                *ret_size = (size_t) n;

                        return 0;
                }
                if (n != -ERANGE)
                        return (int) n;

                n = getxattr_pinned_internal(fd, path, at_flags, by_procfs, name, NULL, 0);
                if (n < 0)
                        return (int) n;

                l = (size_t) n;
        }
}

int xsetxattr_full(
                int fd,
                const char *path,
                int at_flags,
                const char *name,
                const char *value,
                size_t size,
                int xattr_flags) {

        int r;

        assert(fd >= 0 || fd == AT_FDCWD);
        assert((at_flags & ~(AT_SYMLINK_FOLLOW|AT_EMPTY_PATH)) == 0);
        assert(name);
        assert(value);

        if (size == SIZE_MAX)
                size = strlen(value);

        /* Skip the write if the xattr already has the correct value, to avoid
         * unnecessary timestamp changes on the file. Only do this for plain
         * replace mode (xattr_flags == 0) — XATTR_CREATE callers expect
         * -EEXIST when the xattr already exists. */
        _cleanup_free_ char *old_value = NULL;
        size_t old_size;

        if (xattr_flags == 0 &&
            getxattr_at_malloc(fd, path, name, at_flags, &old_value, &old_size) >= 0 &&
            memcmp_nn(old_value, old_size, value, size) == 0)
                return 0;

        if (have_xattrat && !isempty(path)) {
                struct xattr_args args = {
                        .value = PTR_TO_UINT64(value),
                        .size = size,
                        .flags = xattr_flags,
                };

                r = RET_NERRNO(setxattrat(fd, path,
                                          at_flags_normalize_nofollow(at_flags),
                                          name,
                                          &args, sizeof(args)));
                if (r != -ENOSYS) /* No ERRNO_IS_NOT_SUPPORTED here, as EOPNOTSUPP denotes the fs doesn't
                                     support xattr */
                        return r;

                have_xattrat = false;
        }

        _cleanup_close_ int opened_fd = -EBADF;
        bool by_procfs;

        r = normalize_and_maybe_pin_inode(&fd, &path, &at_flags, &opened_fd, &by_procfs);
        if (r < 0)
                return r;

        if (path)
                r = FLAGS_SET(at_flags, AT_SYMLINK_FOLLOW) ? setxattr(path, name, value, size, xattr_flags)
                                                           : lsetxattr(path, name, value, size, xattr_flags);
        else
                r = by_procfs ? setxattr(FORMAT_PROC_FD_PATH(fd), name, value, size, xattr_flags)
                              : fsetxattr(fd, name, value, size, xattr_flags);
        if (r < 0)
                return -errno;

        return 0;
}

