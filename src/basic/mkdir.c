/* SPDX-License-Identifier: LGPL-2.1-or-later */


#include "fd-util.h"
#include "fs-util.h"
#include "mkdir.h"
#include "path-util.h"
#include "stat-util.h"
#include "string-util.h"
#include "user-util.h"

int mkdirat_safe_internal(
                int dir_fd,
                const char *path,
                mode_t mode,
                uid_t uid,
                gid_t gid,
                MkdirFlags flags,
                mkdirat_func_t _mkdirat,
                LabelContext *label_context) {

        struct stat st;
        int r;

        assert(path);
        assert(mode != MODE_INVALID);
        assert(_mkdirat);
        assert((flags & MKDIR_UNSUPPORTED) == 0);
        assert(FLAGS_SET(flags, MKDIR_IGNORE_EXISTING));

        r = _mkdirat(dir_fd, path, mode, label_context);
        if (r >= 0)
                return chmod_and_chown_at(dir_fd, path, mode, uid, gid);
        if (r != -EEXIST)
                return r;

        /* MKDIR_IGNORE_EXISTING: we accept whatever is already there, but it has to still exist. */
        if (fstatat(dir_fd, path, &st, AT_SYMLINK_NOFOLLOW) < 0)
                return -errno;

        return 0;
}

int mkdirat_errno_wrapper(int dirfd, const char *pathname, mode_t mode, LabelContext *label_context) {
        return RET_NERRNO(mkdirat(dirfd, pathname, mode));
}

int mkdirat_parents_internal(int dir_fd, const char *path, mode_t mode, uid_t uid, gid_t gid, MkdirFlags flags, mkdirat_func_t _mkdirat, LabelContext *label_context) {
        const char *e = NULL;
        int r;

        assert(path);
        assert(_mkdirat);
        assert((flags & MKDIR_UNSUPPORTED) == 0);

        if (isempty(path))
                return 0;

        if (!path_is_safe(path))
                return -ENOTDIR;

        /* return immediately if directory exists */
        r = path_find_last_component(path, /* accept_dot_dot= */ false, &e, NULL);
        if (r <= 0) /* r == 0 means path is equivalent to prefix. */
                return r;
        if (e == path)
                return 0;

        assert(e > path);
        assert(*e == '/');

        /* drop the last component */
        path = strndupa_safe(path, e - path);
        r = is_dir_at(dir_fd, path, /* follow= */ true);
        if (r > 0)
                return 0;
        if (r == 0)
                return -ENOTDIR;

        /* create every parent directory in the path, except the last component */
        for (const char *p = path;;) {
                char *s;
                int n;

                n = path_find_first_component(&p, /* accept_dot_dot= */ false, (const char **) &s);
                if (n <= 0)
                        return n;

                assert(p);
                assert(s >= path);
                assert(IN_SET(s[n], '/', '\0'));

                s[n] = '\0';

                r = mkdirat_safe_internal(dir_fd, path, mode, uid, gid, flags | MKDIR_IGNORE_EXISTING, _mkdirat, label_context);
                if (r < 0 && r != -EEXIST)
                        return r;

                s[n] = *p == '\0' ? '\0' : '/';
        }
}

int mkdirat_parents(int dir_fd, const char *path, mode_t mode) {
        return mkdirat_parents_internal(dir_fd, path, mode, UID_INVALID, UID_INVALID, 0, mkdirat_errno_wrapper, /* label_context= */ NULL);
}

