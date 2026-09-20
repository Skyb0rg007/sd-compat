/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <sys/auxv.h>
#include <sys/random.h>

#include "fd-util.h"
#include "io-util.h"
#include "pidfd-util.h"
#include "process-util.h"
#include "sha256.h"
#include "time-util.h"

/* This is a "best effort" kind of thing, but has no real security value. So, this should only be used by
 * random_bytes(), which is not meant for crypto. This could be made better, but we're *not* trying to roll a
 * userspace prng here, or even have forward secrecy, but rather just do the shortest thing that is at least
 * better than libc rand(). */
static void fallback_random_bytes(void *p, size_t n) {
        static thread_local uint64_t fallback_counter = 0;
        struct {
                char label[32];
                uint64_t call_id, block_id;
                usec_t stamp_mono, stamp_real;
                pid_t pid, tid;
                uint64_t pidfdid;
                uint8_t auxval[16];
        } state = {
                /* Arbitrary domain separation to prevent other usage of AT_RANDOM from clashing. */
                .call_id = fallback_counter++,
                .stamp_mono = now(CLOCK_MONOTONIC),
                .stamp_real = now(CLOCK_REALTIME),
                .pid = getpid_cached(),
                .tid = gettid(),
        };

        memcpy(state.label, "systemd fallback random bytes v1", sizeof(state.label));
        memcpy(state.auxval, ULONG_TO_PTR(getauxval(AT_RANDOM)), sizeof(state.auxval));
        (void) pidfd_get_inode_id_self_cached(&state.pidfdid);

        while (n > 0) {
                struct sha256_ctx ctx;

                sha256_init_ctx(&ctx);
                sha256_process_bytes(&state, sizeof(state), &ctx);
                if (n < SHA256_DIGEST_SIZE) {
                        uint8_t partial[SHA256_DIGEST_SIZE];
                        sha256_finish_ctx(&ctx, partial);
                        memcpy(p, partial, n);
                        break;
                }
                sha256_finish_ctx(&ctx, p);
                p = (uint8_t *) p + SHA256_DIGEST_SIZE;
                n -= SHA256_DIGEST_SIZE;
                ++state.block_id;
        }
}

void random_bytes(void *p, size_t n) {
        assert(p || n == 0);

        if (n == 0)
                return;

        for (;;) {
                ssize_t l;

                l = getrandom(p, n, GRND_INSECURE);
                if (l <= 0)
                        break; /* Unexpected error. Give up and fallback to /dev/urandom. */

                if ((size_t) l == n)
                        return; /* Done reading, success. */

                p = (uint8_t *) p + l;
                n -= l;
                /* Interrupted by a signal; keep going. */
        }

        _cleanup_close_ int fd = open("/dev/urandom", O_RDONLY|O_CLOEXEC|O_NOCTTY);
        if (fd >= 0 && loop_read_exact(fd, p, n, false) >= 0)
                return;

        /* This is a terrible fallback. Oh well. */
        fallback_random_bytes(p, n);
}

