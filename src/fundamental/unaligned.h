/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include <stdint.h>

static inline uint64_t unaligned_read_ne64(const void *_u) {
        const struct __attribute__((__packed__, __may_alias__)) { uint64_t x; } *u = _u;

        return u->x;
}

static inline void unaligned_write_ne32(void *_u, uint32_t a) {
        struct __attribute__((__packed__, __may_alias__)) { uint32_t x; } *u = _u;

        u->x = a;
}

