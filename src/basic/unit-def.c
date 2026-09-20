/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "string-table.h"
#include "unit-def.h"

static const char* const unit_type_table[_UNIT_TYPE_MAX] = {
        [UNIT_SERVICE]   = "service",
        [UNIT_SOCKET]    = "socket",
        [UNIT_TARGET]    = "target",
        [UNIT_DEVICE]    = "device",
        [UNIT_MOUNT]     = "mount",
        [UNIT_AUTOMOUNT] = "automount",
        [UNIT_SWAP]      = "swap",
        [UNIT_TIMER]     = "timer",
        [UNIT_PATH]      = "path",
        [UNIT_SLICE]     = "slice",
        [UNIT_SCOPE]     = "scope",
};

DEFINE_STRING_TABLE_LOOKUP(unit_type, UnitType);

/* Keep in sync with man/unit-states.xml */
/* Maps in-progress freezer states to the corresponding finished state */
/* This table maps ExecDirectoryType to the setting it is configured with in the unit */
