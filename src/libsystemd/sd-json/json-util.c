/* SPDX-License-Identifier: LGPL-2.1-or-later */


#include "devnum-util.h"
#include "fd-util.h"
#include "json-util.h"
#include "mountpoint-util.h"
#include "pidref.h"
#include "stat-util.h"
#include "stdio-util.h"
#include "user-util.h"

int json_variant_new_pidref(sd_json_variant **ret, PidRef *pidref) {
        sd_id128_t boot_id = SD_ID128_NULL;
        int r;

        /* Turns a PidRef into a triplet of PID, pidfd inode nr, and the boot ID. The triplet should uniquely
         * identify the process globally, and be good enough to turn back into a pidfd + PidRef */

        if (!pidref_is_set(pidref))
                return sd_json_variant_new_null(ret);

        if (!pidref_is_remote(pidref)) {
                r = pidref_acquire_pidfd_id(pidref);
                if (r < 0 && !ERRNO_IS_NEG_NOT_SUPPORTED(r) && r != -ENOMEDIUM)
                        return r;

                /* If we have the pidfd inode number, also acquire the boot ID, to make things universally unique */
                if (pidref->fd_id > 0) {
                        r = sd_id128_get_boot(&boot_id);
                        if (r < 0)
                                return r;
                }
        }

        return sd_json_buildo(
                        ret,
                        SD_JSON_BUILD_PAIR_INTEGER("pid", pidref->pid),
                        SD_JSON_BUILD_PAIR_CONDITION(pidref->fd_id > 0, "pidfdId", SD_JSON_BUILD_UNSIGNED(pidref->fd_id)),
                        SD_JSON_BUILD_PAIR_CONDITION(!sd_id128_is_null(boot_id), "bootId", SD_JSON_BUILD_ID128(boot_id)));
}

int json_variant_new_devnum(sd_json_variant **ret, dev_t devnum) {
        if (devnum == 0)
                return sd_json_variant_new_null(ret);

        return sd_json_buildo(
                        ret,
                        SD_JSON_BUILD_PAIR_UNSIGNED("major", major(devnum)),
                        SD_JSON_BUILD_PAIR_UNSIGNED("minor", minor(devnum)));
}

static int json_variant_new_stat(sd_json_variant **ret, const struct stat *st) {
        char mode[STRLEN("0755")+1];

        assert(st);

        if (!stat_is_set(st))
                return sd_json_variant_new_null(ret);

        xsprintf(mode, "%04o", st->st_mode & ~S_IFMT);

        return sd_json_buildo(
                        ret,
                        JSON_BUILD_PAIR_DEVNUM("dev", st->st_dev),
                        SD_JSON_BUILD_PAIR_UNSIGNED("inode", st->st_ino),
                        JSON_BUILD_PAIR_STRING_NON_EMPTY("type", inode_type_to_string(st->st_mode)),
                        SD_JSON_BUILD_PAIR_STRING("mode", mode),
                        SD_JSON_BUILD_PAIR_UNSIGNED("linkCount", st->st_nlink),
                        SD_JSON_BUILD_PAIR_UNSIGNED("uid", st->st_uid),
                        SD_JSON_BUILD_PAIR_UNSIGNED("gid", st->st_gid),
                        SD_JSON_BUILD_PAIR_CONDITION(
                                        S_ISCHR(st->st_mode) || S_ISBLK(st->st_mode),
                                        "rdev",
                                        JSON_BUILD_DEVNUM(st->st_rdev)),
                        SD_JSON_BUILD_PAIR_UNSIGNED("size", st->st_size),
                        SD_JSON_BUILD_PAIR_UNSIGNED("blockSize", st->st_blksize),
                        SD_JSON_BUILD_PAIR_UNSIGNED("blocks", st->st_blocks));
}

static int json_variant_new_file_handle(sd_json_variant **ret, const struct file_handle *fid) {
        assert(ret);

        if (!fid)
                return sd_json_variant_new_null(ret);

        return sd_json_buildo(
                        ret,
                        SD_JSON_BUILD_PAIR_INTEGER("type", fid->handle_type),
                        SD_JSON_BUILD_PAIR_BASE64("handle", fid->f_handle, fid->handle_bytes));
}

int json_variant_new_fd_info(sd_json_variant **ret, int fd) {
        _cleanup_(sd_json_variant_unrefp) sd_json_variant *v = NULL, *w = NULL;
        _cleanup_free_ char *path = NULL;
        _cleanup_free_ struct file_handle *fid = NULL;
        struct stat st;
        int mntid = -1, r;

        assert(fd >= 0 || fd == AT_FDCWD);

        r = fd_get_path(fd, &path);
        if (r < 0)
                return r;

        /* If AT_FDCWD is specified, show information about the current working directory.  */
        if (fstatat(fd, "", &st, AT_EMPTY_PATH) < 0)
                return -errno;

        r = json_variant_new_stat(&v, &st);
        if (r < 0)
                return r;

        r = name_to_handle_at_try_fid(fd, "", &fid, &mntid, /* ret_unique_mnt_id = */ NULL, AT_EMPTY_PATH);
        if (r < 0 && is_name_to_handle_at_fatal_error(r))
                return r;

        r = json_variant_new_file_handle(&w, fid);
        if (r < 0)
                return r;

        return sd_json_buildo(
                        ret,
                        JSON_BUILD_PAIR_INTEGER_NON_NEGATIVE("fd", fd),
                        SD_JSON_BUILD_PAIR_STRING("path", path),
                        SD_JSON_BUILD_PAIR_VARIANT("stat", v),
                        JSON_BUILD_PAIR_INTEGER_NON_NEGATIVE("mountId", mntid),
                        SD_JSON_BUILD_PAIR_VARIANT("fileHandle", w));
}

