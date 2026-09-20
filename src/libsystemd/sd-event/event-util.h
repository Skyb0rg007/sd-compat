/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include "sd-event.h"

#include "forward.h"

#define PROTECT_EVENT(e)                                                \
        _unused_ _cleanup_(sd_event_unrefp) sd_event *_ref = sd_event_ref(e);
