/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include <endian.h>

#include "forward.h"

#include "../fundamental/unaligned.h" /* IWYU pragma: export */

/* BE */

/* LE */

static inline uint64_t unaligned_read_le64(const void *_u) {
        const struct __attribute__((__packed__, __may_alias__)) { uint64_t x; } *u = _u;

        return le64toh(u->x);
}

