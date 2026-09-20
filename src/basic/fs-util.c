/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <sys/mount.h>
#include <unistd.h>

#include "errno-util.h"
#include "fd-util.h"
#include "fs-util.h"
#include "path-util.h"
#include "process-util.h"
#include "stat-util.h"
#include "strv.h"

int readlinkat_malloc(int fd, const char *p, char **ret) {
        size_t l = PATH_MAX;

        assert(fd >= 0 || fd == AT_FDCWD);

        if (fd < 0 && isempty(p))
                return -EISDIR; /* In this case, the fd points to the current working directory, and is
                                 * definitely not a symlink. Let's return earlier. */

        for (;;) {
                _cleanup_free_ char *c = NULL;
                ssize_t n;

                c = new(char, l+1);
                if (!c)
                        return -ENOMEM;

                n = readlinkat(fd, strempty(p), c, l);
                if (n < 0)
                        return -errno;

                if ((size_t) n < l) {
                        c[n] = 0;

                        if (ret)
                                *ret = TAKE_PTR(c);

                        return 0;
                }

                if (l > (SSIZE_MAX-1)/2) /* readlinkat() returns an ssize_t, and we want an extra byte for a
                                          * trailing NUL, hence do an overflow check relative to SSIZE_MAX-1
                                          * here */
                        return -EFBIG;

                l *= 2;
        }
}

int chmod_and_chown_at(int dir_fd, const char *path, mode_t mode, uid_t uid, gid_t gid) {
        _cleanup_close_ int fd = -EBADF;

        assert(dir_fd >= 0 || dir_fd == AT_FDCWD);

        if (path) {
                /* Let's acquire an O_PATH fd, as precaution to change mode/owner on the same file */
                fd = openat(dir_fd, path, O_PATH|O_CLOEXEC|O_NOFOLLOW);
                if (fd < 0)
                        return -errno;
                dir_fd = fd;

        } else if (dir_fd == AT_FDCWD) {
                /* Let's acquire an O_PATH fd of the current directory */
                fd = openat(dir_fd, ".", O_PATH|O_CLOEXEC|O_NOFOLLOW|O_DIRECTORY);
                if (fd < 0)
                        return -errno;
                dir_fd = fd;
        }

        return fchmod_and_chown(dir_fd, mode, uid, gid);
}

int fchmod_and_chown_with_fallback(int fd, const char *path, mode_t mode, uid_t uid, gid_t gid) {
        bool do_chown, do_chmod;
        struct stat st;
        int r;

        /* Change ownership and access mode of the specified fd. Tries to do so safely, ensuring that at no
         * point in time the access mode is above the old access mode under the old ownership or the new
         * access mode under the new ownership. Note: this call tries hard to leave the access mode
         * unaffected if the uid/gid is changed, i.e. it undoes implicit suid/sgid dropping the kernel does
         * on chown().
         *
         * This call is happy with O_PATH fds.
         *
         * If path is given, allow a fallback path which does not use /proc/self/fd/. On any normal system
         * /proc will be mounted, but in certain improperly assembled environments it might not be. This is
         * less secure (potential TOCTOU), so should only be used after consideration. */

        if (fstat(fd, &st) < 0)
                return -errno;

        do_chown =
                (uid != UID_INVALID && st.st_uid != uid) ||
                (gid != GID_INVALID && st.st_gid != gid);

        do_chmod =
                !S_ISLNK(st.st_mode) && /* chmod is not defined on symlinks */
                ((mode != MODE_INVALID && ((st.st_mode ^ mode) & 07777) != 0) ||
                 do_chown); /* If we change ownership, make sure we reset the mode afterwards, since chown()
                             * modifies the access mode too */

        if (mode == MODE_INVALID)
                mode = st.st_mode; /* If we only shall do a chown(), save original mode, since chown() might break it. */
        else if ((mode & S_IFMT) != 0 && ((mode ^ st.st_mode) & S_IFMT) != 0)
                return -EINVAL; /* insist on the right file type if it was specified */

        if (do_chown && do_chmod) {
                mode_t minimal = st.st_mode & mode; /* the subset of the old and the new mask */

                if (((minimal ^ st.st_mode) & 07777) != 0) {
                        r = fchmod_opath(fd, minimal & 07777);
                        if (r < 0) {
                                if (!path || r != -ENOSYS)
                                        return r;

                                /* Fallback path which doesn't use /proc/self/fd/. */
                                if (chmod(path, minimal & 07777) < 0)
                                        return -errno;
                        }
                }
        }

        if (do_chown)
                if (fchownat(fd, "", uid, gid, AT_EMPTY_PATH) < 0)
                        return -errno;

        if (do_chmod) {
                r = fchmod_opath(fd, mode & 07777);
                if (r < 0) {
                        if (!path || r != -ENOSYS)
                                return r;

                        /* Fallback path which doesn't use /proc/self/fd/. */
                        if (chmod(path, mode & 07777) < 0)
                                return -errno;
                }
        }

        return do_chown || do_chmod;
}

int fchmod_opath(int fd, mode_t m) {
        /* This function operates also on fd that might have been opened with
         * O_PATH. The tool set we have is non-intuitive:
         * - fchmod(2) only operates on open files (i. e., fds with an open file description);
         * - fchmodat(2) does not have a flag arg like fchownat(2) does, so no way to pass AT_EMPTY_PATH;
         *   + it should not be confused with the libc fchmodat(3) interface, which adds 4th flag argument,
         *     and supports AT_EMPTY_PATH since v2.39 (previously only supported AT_SYMLINK_NOFOLLOW). So if
         *     the kernel has fchmodat2(2), since v2.39 glibc will call into it directly. If the kernel
         *     doesn't, or glibc is older than v2.39, glibc's internal fallback will return EINVAL if
         *     AT_EMPTY_PATH is passed.
         * - fchmodat2(2) supports all the AT_* flags, but is still very recent.
         *
         * We try to use fchmodat(3) first, and on EINVAL fall back to fchmodat2(), and, if that is also not
         * supported, resort to the /proc/self/fd dance. */

        assert(fd >= 0);

        if (fchmodat(fd, "", m, AT_EMPTY_PATH) >= 0)
                return 0;
        if (errno == EINVAL && fchmodat2(fd, "", m, AT_EMPTY_PATH) >= 0) /* glibc too old? */
                return 0;
        if (!IN_SET(errno, ENOSYS, EPERM)) /* Some container managers block unknown syscalls with EPERM */
                return -errno;

        if (chmod(FORMAT_PROC_FD_PATH(fd), m) < 0) {
                if (errno != ENOENT)
                        return -errno;

                return proc_fd_enoent_errno();
        }

        return 0;
}

int access_nofollow(const char *path, int mode) {
        return RET_NERRNO(faccessat(AT_FDCWD, path, mode, AT_SYMLINK_NOFOLLOW));
}

static int getenv_tmp_dir(const char **ret_path) {
        int r, ret = 0;

        assert(ret_path);

        /* We use the same order of environment variables python uses in tempfile.gettempdir():
         * https://docs.python.org/3/library/tempfile.html#tempfile.gettempdir */
        FOREACH_STRING(n, "TMPDIR", "TEMP", "TMP") {
                const char *e;

                e = secure_getenv(n);
                if (!e)
                        continue;
                if (!path_is_absolute(e)) {
                        r = -ENOTDIR;
                        goto next;
                }
                if (!path_is_normalized(e)) {
                        r = -EPERM;
                        goto next;
                }

                r = is_dir(e, true);
                if (r < 0)
                        goto next;
                if (r == 0) {
                        r = -ENOTDIR;
                        goto next;
                }

                *ret_path = e;
                return 1;

        next:
                /* Remember first error, to make this more debuggable */
                if (ret >= 0)
                        ret = r;
        }

        if (ret < 0)
                return ret;

        *ret_path = NULL;
        return ret;
}

static int tmp_dir_internal(const char *def, const char **ret) {
        const char *e;
        int r, k;

        assert(def);
        assert(ret);

        r = getenv_tmp_dir(&e);
        if (r > 0) {
                *ret = e;
                return 0;
        }

        k = is_dir(def, /* follow= */ true);
        if (k == 0)
                k = -ENOTDIR;
        if (k < 0)
                return RET_GATHER(r, k);

        *ret = def;
        return 0;
}

int var_tmp_dir(const char **ret) {
        assert(ret);

        /* Returns the location for "larger" temporary files, that is backed by physical storage if available, and thus
         * even might survive a boot: /var/tmp. If $TMPDIR (or related environment variables) are set, its value is
         * returned preferably however. Note that both this function and tmp_dir() below are affected by $TMPDIR,
         * making it a variable that overrides all temporary file storage locations. */

        return tmp_dir_internal("/var/tmp", ret);
}

int tmp_dir(const char **ret) {
        assert(ret);

        /* Similar to var_tmp_dir() above, but returns the location for "smaller" temporary files, which is usually
         * backed by an in-memory file system: /tmp. */

        return tmp_dir_internal("/tmp", ret);
}

char *rmdir_and_free(char *p) {
        PROTECT_ERRNO;

        if (!p)
                return NULL;

        (void) rmdir(p);
        return mfree(p);
}

int openat_report_new(int dirfd, const char *pathname, int flags, mode_t mode, bool *ret_newly_created) {
        int fd;

        /* Just like openat(), but adds one thing: optionally returns whether we created the file anew or if
         * it already existed before. This is only relevant if O_CREAT is set without O_EXCL, and thus will
         * shortcut to openat() otherwise.
         *
         * Note that this routine is a bit more strict with symlinks than regular openat() is. If O_NOFOLLOW
         * is not specified, then we'll follow the symlink when opening an existing file but we will *not*
         * follow it when creating a new one (because that's a terrible UNIX misfeature and generally a
         * security hole). */

        if (!FLAGS_SET(flags, O_CREAT) || FLAGS_SET(flags, O_EXCL)) {
                fd = openat(dirfd, pathname, flags, mode);
                if (fd < 0)
                        return -errno;

                if (ret_newly_created)
                        *ret_newly_created = FLAGS_SET(flags, O_CREAT);
                return fd;
        }

        for (unsigned attempts = 7;;) {
                /* First, attempt to open without O_CREAT/O_EXCL, i.e. open existing file */
                fd = openat(dirfd, pathname, flags & ~(O_CREAT | O_EXCL), mode);
                if (fd >= 0) {
                        if (ret_newly_created)
                                *ret_newly_created = false;
                        return fd;
                }
                if (errno != ENOENT)
                        return -errno;

                /* So the file didn't exist yet, hence create it with O_CREAT/O_EXCL/O_NOFOLLOW. */
                fd = openat(dirfd, pathname, flags | O_CREAT | O_EXCL | O_NOFOLLOW, mode);
                if (fd >= 0) {
                        if (ret_newly_created)
                                *ret_newly_created = true;
                        return fd;
                }
                if (errno != EEXIST)
                        return -errno;

                /* Hmm, so now we got EEXIST? Then someone might have created the file between the first and
                 * second call to openat(). Let's try again but with a limit so we don't spin forever. */

                if (--attempts == 0) /* Give up eventually, somebody is playing with us */
                        return -EEXIST;
        }
}

int xopenat_full_label(int dir_fd, const char *path, int open_flags, XOpenFlags xopen_flags, mode_t mode, LabelContext *label_context) {
        _cleanup_close_ int fd = -EBADF;
        bool made_dir = false, made_file = false;
        int r;

        assert(wildcard_fd_is_valid(dir_fd));
        assert((xopen_flags & XO_UNSUPPORTED) == 0);
        assert(!label_context);

        /* This is like openat(), but has a few tricks up its sleeves, extending behaviour:
         *
         *   • O_DIRECTORY|O_CREAT is supported, which causes a directory to be created, and immediately
         *     opened.
         *
         *   • If the path is specified NULL or empty, behaves like fd_reopen().
         *
         *   • If mode is specified as MODE_INVALID, we'll use 0755 for dirs, and 0644 for regular files.
         *
         *   • The dir fd can be passed as XAT_FDROOT, in which case any relative paths will be taken relative to the root fs.
         */

        if (mode == MODE_INVALID)
                mode = (open_flags & O_DIRECTORY) ? 0755 : 0644;

        if (isempty(path)) {
                assert(!FLAGS_SET(open_flags, O_CREAT|O_EXCL));
                open_flags &= ~O_NOFOLLOW;

                return fd_reopen(dir_fd, open_flags);
        }

        _cleanup_close_ int _dir_fd = -EBADF;
        if (dir_fd == XAT_FDROOT) {
                if (path_is_absolute(path))
                        dir_fd = AT_FDCWD;
                else {
                        _dir_fd = open("/", O_CLOEXEC|O_DIRECTORY|O_PATH);
                        if (_dir_fd < 0)
                                return -errno;

                        dir_fd = _dir_fd;
                }
        }

        if (FLAGS_SET(open_flags, O_DIRECTORY|O_CREAT)) {
                r = RET_NERRNO(mkdirat(dir_fd, path, mode));
                if (r == -EEXIST) {
                        if (FLAGS_SET(open_flags, O_EXCL))
                                return -EEXIST;
                } else if (r < 0)
                        return r;
                else
                        made_dir = true;

                open_flags &= ~(O_EXCL|O_CREAT);
        }

        fd = openat_report_new(dir_fd, path, open_flags, mode, &made_file);
        if (fd < 0) {
                r = fd;
                goto error;
        }

        return TAKE_FD(fd);

error:
        if (made_dir || made_file)
                (void) unlinkat(dir_fd, path, made_dir ? AT_REMOVEDIR : 0);

        return r;
}
