/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include "forward.h"
#include "parse-util.h"

typedef enum PressureResource {
        PRESSURE_MEMORY,
        PRESSURE_CPU,
        PRESSURE_IO,
        _PRESSURE_RESOURCE_MAX,
        _PRESSURE_RESOURCE_INVALID = -EINVAL,
} PressureResource;

/* Was the kernel compiled with CONFIG_PSI=y? 1 if yes, 0 if not, negative on error. */
int is_pressure_supported(void);

/* Metadata for each pressure resource type, for use in sd-event and PID 1 */
typedef struct PressureResourceInfo {
        const char *name;       /* "memory", "cpu", "io" */
        const char *env_watch;  /* "MEMORY_PRESSURE_WATCH", etc. */
        const char *env_write;  /* "MEMORY_PRESSURE_WRITE", etc. */
} PressureResourceInfo;

extern const PressureResourceInfo pressure_resource_info[_PRESSURE_RESOURCE_MAX];

static inline const PressureResourceInfo* pressure_resource_get_info(PressureResource resource) {
        assert(resource >= 0 && resource < _PRESSURE_RESOURCE_MAX);
        return &pressure_resource_info[resource];
}

/* Default parameters for pressure watch logic in sd-event and PID 1 */
#define PRESSURE_DEFAULT_TYPE "some"
#define PRESSURE_DEFAULT_THRESHOLD_USEC (200 * USEC_PER_MSEC)
#define PRESSURE_DEFAULT_WINDOW_USEC (2 * USEC_PER_SEC)
