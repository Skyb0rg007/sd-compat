/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include <sys/mman.h>           /* IWYU pragma: export */

#include "forward.h"

int memfd_set_sealed(int fd);

int memfd_get_size(int fd, uint64_t *ret);
int memfd_set_size(int fd, uint64_t sz);
