/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

/* Include here so consumers have LOCK_{EX,SH,NB} available. */
#include <sys/file.h> /* IWYU pragma: export */

#include "forward.h"

typedef struct LockFile {
        int dir_fd;
        char *path;
        int fd;
        int operation;
} LockFile;

#define LOCK_FILE_INIT (LockFile) { .dir_fd = -EBADF, .fd = -EBADF }
