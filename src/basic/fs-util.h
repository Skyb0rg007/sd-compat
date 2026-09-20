/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include "forward.h"
#include "lock-util.h"

/* The following macros add 1 when converting things, since 0 is a valid mode, while the pointer
 * NULL is special */

int readlinkat_malloc(int fd, const char *p, char **ret);
static inline int readlink_malloc(const char *p, char **ret) {
        return readlinkat_malloc(AT_FDCWD, p, ret);
}

int chmod_and_chown_at(int dir_fd, const char *path, mode_t mode, uid_t uid, gid_t gid);
int fchmod_and_chown_with_fallback(int fd, const char *path, mode_t mode, uid_t uid, gid_t gid);
static inline int fchmod_and_chown(int fd, mode_t mode, uid_t uid, gid_t gid) {
        return fchmod_and_chown_with_fallback(fd, NULL, mode, uid, gid); /* no fallback */
}

int fchmod_opath(int fd, mode_t m);

int access_nofollow(const char *path, int mode);

int tmp_dir(const char **ret);
int var_tmp_dir(const char **ret);

/* Useful for usage with _cleanup_(), removes a directory and frees the pointer */
char *rmdir_and_free(char *p);
DEFINE_TRIVIAL_CLEANUP_FUNC(char*, rmdir_and_free);

typedef enum XOpenFlags {
        XO_LABEL             = 1 << 0, /* When creating: relabel */
        XO_SUBVOLUME         = 1 << 1, /* When creating as directory: make it a subvolume */
        XO_NOCOW             = 1 << 2, /* Enable NOCOW mode after opening */
        XO_COW               = 1 << 3, /* Enable COW mode after opening */
        XO_REGULAR           = 1 << 4, /* Fail if the inode is not a regular file */
        XO_SOCKET            = 1 << 5, /* Fail if the inode is not a socket */
        XO_TRIGGER_AUTOMOUNT = 1 << 6, /* Trigger automounts via open_tree(). Requires O_PATH. */
        XO_AUTO_RW_RO        = 1 << 7, /* Open in O_RDWR mode if possible, O_RDONLY if not */
} XOpenFlags;

/* None of the above is implemented anymore, and xopenat_full_label()'s label_context must be NULL for the
 * same reason: every caller opens a plain inode. */
#define XO_UNSUPPORTED                          \
        (XO_LABEL |                             \
         XO_SUBVOLUME |                         \
         XO_NOCOW |                             \
         XO_COW |                               \
         XO_REGULAR |                           \
         XO_SOCKET |                            \
         XO_TRIGGER_AUTOMOUNT |                 \
         XO_AUTO_RW_RO)

int openat_report_new(int dirfd, const char *pathname, int flags, mode_t mode, bool *ret_newly_created);

int xopenat_full_label(int dir_fd, const char *path, int open_flags, XOpenFlags xopen_flags, mode_t mode, LabelContext *label_context);
static inline int xopenat_full(int dir_fd, const char *path, int open_flags, XOpenFlags xopen_flags, mode_t mode) {
        return xopenat_full_label(dir_fd, path, open_flags, xopen_flags, mode, /* label_context= */ NULL);
}
static inline int xopenat(int dir_fd, const char *path, int open_flags) {
        return xopenat_full(dir_fd, path, open_flags, 0, MODE_INVALID);
}

static inline int at_flags_normalize_nofollow(int flags) {
        if (FLAGS_SET(flags, AT_SYMLINK_FOLLOW)) {
                assert(!FLAGS_SET(flags, AT_SYMLINK_NOFOLLOW));
                flags &= ~AT_SYMLINK_FOLLOW;
        } else
                flags |= AT_SYMLINK_NOFOLLOW;
        return flags;
}

static inline int at_flags_normalize_follow(int flags) {
        if (FLAGS_SET(flags, AT_SYMLINK_NOFOLLOW)) {
                assert(!FLAGS_SET(flags, AT_SYMLINK_FOLLOW));
                flags &= ~AT_SYMLINK_NOFOLLOW;
        } else
                flags |= AT_SYMLINK_FOLLOW;
        return flags;
}
