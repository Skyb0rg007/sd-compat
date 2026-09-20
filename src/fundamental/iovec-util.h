/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#if !SD_BOOT
#include <sys/uio.h>
#endif

#include "assert-util.h"         /* IWYU pragma: keep */
#include "macro.h"

#if SD_BOOT
/* struct iovec is a POSIX userspace construct. Let's introduce it also in EFI mode, it's just so useful */
struct iovec {
        void *iov_base;
        size_t iov_len;
};
#endif

/* This accepts both const and non-const pointers */
#define IOVEC_MAKE(base, len)                                           \
        (struct iovec) {                                                \
                .iov_base = (void*) (base),                             \
                .iov_len = (len),                                       \
        }

static inline struct iovec* iovec_shift(const struct iovec *iovec, size_t shift, struct iovec *ret) {
        assert(iovec);
        assert(ret);

        /* This returns an empty iovec when 'shift' is larger or equals to the input iovec length.
         * The 'iovec' and 'ret' can point to the same object. */

        *ret = IOVEC_MAKE(iovec->iov_len > shift ? (uint8_t*) iovec->iov_base + shift : NULL,
                          LESS_BY(iovec->iov_len, shift));
        return ret;
}

#define IOVEC_SHIFT(iov, shift)                         \
        *iovec_shift(iov, shift, &(struct iovec){})

static inline struct iovec* iovec_inc(struct iovec *iovec, size_t shift) {
        return iovec_shift(iovec, shift, iovec);
}

static inline bool iovec_is_set(const struct iovec *iovec) {
        /* Checks if the iovec points to a non-empty chunk of memory */
        return iovec && iovec->iov_len > 0 && iovec->iov_base;
}

