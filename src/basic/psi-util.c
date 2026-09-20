/* SPDX-License-Identifier: LGPL-2.1-or-later */


#include "errno-util.h"
#include "fileio.h"
#include "psi-util.h"
#include "strv.h"

const PressureResourceInfo pressure_resource_info[_PRESSURE_RESOURCE_MAX] = {
        [PRESSURE_MEMORY] = {
                .name      = "memory",
                .env_watch = "MEMORY_PRESSURE_WATCH",
                .env_write = "MEMORY_PRESSURE_WRITE",
        },
        [PRESSURE_CPU] = {
                .name      = "cpu",
                .env_watch = "CPU_PRESSURE_WATCH",
                .env_write = "CPU_PRESSURE_WRITE",
        },
        [PRESSURE_IO] = {
                .name      = "io",
                .env_watch = "IO_PRESSURE_WATCH",
                .env_write = "IO_PRESSURE_WRITE",
        },
};

int is_pressure_supported(void) {
        static thread_local int cached = -1;
        int r;

        /* The pressure files, both under /proc/ and in cgroups, will exist even if the kernel has PSI
         * support disabled; we have to read the file to make sure it doesn't return -EOPNOTSUPP */

        if (cached >= 0)
                return cached;

        FOREACH_STRING(p, "/proc/pressure/cpu", "/proc/pressure/io", "/proc/pressure/memory") {
                r = read_virtual_file(p, 0, NULL, NULL);
                if (r == -ENOENT || ERRNO_IS_NEG_NOT_SUPPORTED(r))
                        return (cached = false);
                if (r < 0)
                        return r;
        }

        return (cached = true);
}
