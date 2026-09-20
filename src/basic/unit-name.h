/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include "forward.h"

#define UNIT_NAME_MAX 256

typedef enum UnitNameFlags {
        UNIT_NAME_PLAIN    = 1 << 0, /* Allow foo.service */
        UNIT_NAME_TEMPLATE = 1 << 1, /* Allow foo@.service */
        UNIT_NAME_INSTANCE = 1 << 2, /* Allow foo@bar.service */
        UNIT_NAME_ANY = UNIT_NAME_PLAIN|UNIT_NAME_TEMPLATE|UNIT_NAME_INSTANCE,
        _UNIT_NAME_INVALID = -EINVAL,
} UnitNameFlags;

bool unit_name_is_valid(const char *n, UnitNameFlags flags) _pure_;

UnitNameFlags unit_name_to_instance(const char *n, char **ret);
