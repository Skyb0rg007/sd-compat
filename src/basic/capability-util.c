/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <stdatomic.h>
#include <sys/prctl.h> /* IWYU pragma: keep */
#include <sys/syscall.h>

#include "bitfield.h"
#include "capability-util.h"
#include "log.h"
#include "parse-util.h"
#include "process-util.h"

int capability_get(CapabilityQuintet *ret) {
        assert(ret);

        struct __user_cap_header_struct hdr = {
                .version = _LINUX_CAPABILITY_VERSION_3,
                .pid = getpid_cached(),
        };

        assert_cc(_LINUX_CAPABILITY_U32S_3 == 2);
        struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];
        if (syscall(SYS_capget, &hdr, data) < 0)
                return -errno;

        *ret = (CapabilityQuintet) {
                .effective = (uint64_t) data[0].effective | ((uint64_t) data[1].effective << 32),
                .bounding = UINT64_MAX,
                .inheritable = (uint64_t) data[0].inheritable | ((uint64_t) data[1].inheritable << 32),
                .permitted = (uint64_t) data[0].permitted | ((uint64_t) data[1].permitted << 32),
                .ambient = UINT64_MAX,
        };
        return 0;
}

unsigned cap_last_cap(void) {
        static atomic_int saved = INT_MAX;
        int r, c;

        c = saved;
        if (c != INT_MAX)
                return c;

        /* Available since linux-3.2 */
        _cleanup_free_ char *content = NULL;
        r = read_one_line_file("/proc/sys/kernel/cap_last_cap", &content);
        if (r < 0)
                log_debug_errno(r, "Failed to read /proc/sys/kernel/cap_last_cap, ignoring: %m");
        else {
                r = safe_atoi(content, &c);
                if (r < 0)
                        log_debug_errno(r, "Failed to parse /proc/sys/kernel/cap_last_cap, ignoring: %m");
                else {
                        if (c > CAP_LIMIT) /* Safety for the future: if one day the kernel learns more than
                                            * 64 caps, then we are in trouble (since we, as much userspace
                                            * and kernel space store capability masks in uint64_t types). We
                                            * also want to use UINT64_MAX as marker for "unset". Hence let's
                                            * hence protect ourselves against that and always cap at 62 for
                                            * now. */
                                c = CAP_LIMIT;

                        saved = c;
                        return c;
                }
        }

        /* Fall back to syscall-probing for pre linux-3.2, or where /proc/ is not mounted */
        unsigned long p = (unsigned long) MIN(CAP_LAST_CAP, CAP_LIMIT);

        if (prctl_safe(PR_CAPBSET_READ, p, 0, 0, 0) < 0) {

                /* Hmm, look downwards, until we find one that works */
                for (p--; p > 0; p--)
                        if (prctl_safe(PR_CAPBSET_READ, p, 0, 0, 0) >= 0)
                                break;

        } else {

                /* Hmm, look upwards, until we find one that doesn't work */
                for (; p < CAP_LIMIT; p++)
                        if (prctl_safe(PR_CAPBSET_READ, p+1, 0, 0, 0) < 0)
                                break;
        }

        c = (int) p;
        saved = c;
        return c;
}

int have_effective_cap(unsigned cap) {
        CapabilityQuintet q;
        int r;

        assert(cap <= CAP_LIMIT);

        r = capability_get(&q);
        if (r < 0)
                return r;

        return BIT_SET(q.effective, cap);
}

