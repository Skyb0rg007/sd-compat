/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include "forward.h"

int getxattr_at_malloc(int fd, const char *path, const char *name, int at_flags, char **ret, size_t *ret_size);

int xsetxattr_full(
                int fd,
                const char *path,
                int at_flags,
                const char *name,
                const char *value,
                size_t size,
                int xattr_flags);
static inline int xsetxattr(
                int fd,
                const char *path,
                int at_flags,
                const char *name,
                const char *value) {
        return xsetxattr_full(fd, path, at_flags, name, value, SIZE_MAX, 0);
}
