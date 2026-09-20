/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include "forward.h"

typedef enum MkdirFlags {
        MKDIR_FOLLOW_SYMLINK  = 1 << 0,
        MKDIR_IGNORE_EXISTING = 1 << 1,  /* Quietly accept a preexisting directory (or file) */
        MKDIR_WARN_MODE       = 1 << 2,  /* Log at LOG_WARNING when mode doesn't match */
} MkdirFlags;

int mkdirat_errno_wrapper(int dirfd, const char *pathname, mode_t mode, LabelContext *label_context);

int mkdirat_parents(int dir_fd, const char *path, mode_t mode);
static inline int mkdir_parents(const char *path, mode_t mode) {
        return mkdirat_parents(AT_FDCWD, path, mode);
}

/* The following are used to implement the mkdir_xyz_label() calls, don't use otherwise. */
typedef int (*mkdirat_func_t)(int dir_fd, const char *pathname, mode_t mode, LabelContext *label_context);
int mkdirat_safe_internal(int dir_fd, const char *path, mode_t mode, uid_t uid, gid_t gid, MkdirFlags flags, mkdirat_func_t _mkdir, LabelContext *label_context);
int mkdirat_parents_internal(int dir_fd, const char *path, mode_t mode, uid_t uid, gid_t gid, MkdirFlags flags, mkdirat_func_t _mkdirat, LabelContext *label_context);
