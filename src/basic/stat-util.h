/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include <sys/stat.h>           /* IWYU pragma: export */
#include <sys/statfs.h>         /* IWYU pragma: export */

#include "forward.h"

int stat_verify_regular(const struct stat *st);
int statx_verify_regular(const struct statx *stx);
int verify_regular_at(int fd, const char *path, bool follow);
int fd_verify_regular(int fd);

int stat_verify_directory(const struct stat *st);
int statx_verify_directory(const struct statx *stx);
int fd_verify_directory(int fd);
int is_dir_at(int fd, const char *path, bool follow);
int is_dir(const char *path, bool follow);

int stat_verify_socket(const struct stat *st);
int statx_verify_socket(const struct statx *stx);
int fd_verify_socket(int fd);

typedef enum XStatXFlags {
        XSTATX_MNT_ID_BEST = 1 << 0, /* Like STATX_MNT_ID_UNIQUE if available, STATX_MNT_ID otherwise */
} XStatXFlags;

int xstatx_full(int fd,
                const char *path,
                int statx_flags,
                XStatXFlags xstatx_flags,
                unsigned mandatory_mask,
                unsigned optional_mask,
                uint64_t mandatory_attributes,
                struct statx *ret);

static inline int xstatx(
                int fd,
                const char *path,
                int statx_flags,
                unsigned mandatory_mask,
                struct statx *ret) {

        return xstatx_full(fd, path, statx_flags, 0, mandatory_mask, 0, 0, ret);
}

int inode_same_at(int fda, const char *filea, int fdb, const char *fileb, int flags);
static inline int inode_same(const char *filea, const char *fileb, int flags) {
        return inode_same_at(AT_FDCWD, filea, AT_FDCWD, fileb, flags);
}
static inline int fd_inode_same(int fda, int fdb) {
        return inode_same_at(fda, NULL, fdb, NULL, AT_EMPTY_PATH);
}

/* The .f_type field of struct statfs is really weird defined on
 * different archs. Let's give its type a name. */
typedef typeof_field(struct statfs, f_type) statfs_f_type_t;

bool is_fs_type(const struct statfs *s, statfs_f_type_t magic_value) _pure_;
int is_fs_type_at(int dir_fd, const char *path, statfs_f_type_t magic_value);
static inline int fd_is_fs_type(int fd, statfs_f_type_t magic_value) {
        return is_fs_type_at(fd, NULL, magic_value);
}
static inline int path_is_fs_type(const char *path, statfs_f_type_t magic_value) {
        return is_fs_type_at(AT_FDCWD, path, magic_value);
}

/* Because statfs.t_type can be int on some architectures, we have to cast
 * the const magic to the type, otherwise the compiler warns about
 * signed/unsigned comparison, because the magic can be 32 bit unsigned.
 */
#define F_TYPE_EQUAL(a, b) (a == (typeof(a)) b)

int proc_mounted(void);

bool stat_inode_same(const struct stat *a, const struct stat *b);

bool statx_inode_same(const struct statx *a, const struct statx *b);
int statx_mount_same(const struct statx *a, const struct statx *b);

int xstatfsat(int dir_fd, const char *path, struct statfs *ret);

DECLARE_STRING_TABLE_LOOKUP(inode_type, mode_t);

/* Macros that check whether the stat/statx structures have been initialized already. For "struct stat" we
 * use a check for .st_dev being non-zero, since the kernel unconditionally fills that in, mapping the file
 * to its originating superblock, regardless if the fs is block based or virtual (we also check for .st_mode
 * being MODE_INVALID, since we use that as an invalid marker for separate mode_t fields). For "struct statx"
 * we use the .stx_mask field, which must be non-zero if any of the fields have already been initialized. */
static inline bool stat_is_set(const struct stat *st) {
        return st && st->st_dev != 0 && st->st_mode != MODE_INVALID;
}
static inline bool statx_is_set(const struct statx *sx) {
        return sx && sx->stx_mask != 0;
}
