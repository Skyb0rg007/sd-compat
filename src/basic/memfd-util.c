/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <sys/stat.h>
#include <unistd.h>

#include "errno-util.h"
#include "fd-util.h"
#include "process-util.h"

static int memfd_add_seals(int fd, unsigned seals) {
        assert(fd >= 0);

        return RET_NERRNO(fcntl(fd, F_ADD_SEALS, seals));
}

int memfd_set_sealed(int fd) {
        return memfd_add_seals(fd, F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE);
}

int memfd_get_size(int fd, uint64_t *ret) {
        struct stat stat;

        assert(fd >= 0);
        assert(ret);

        if (fstat(fd, &stat) < 0)
                return -errno;

        *ret = stat.st_size;
        return 0;
}

int memfd_set_size(int fd, uint64_t sz) {
        assert(fd >= 0);

        return RET_NERRNO(ftruncate(fd, sz));
}

