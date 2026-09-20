/* SPDX-License-Identifier: LGPL-2.1-or-later */


#include "string-util.h"

bool session_id_valid(const char *id) {

        if (isempty(id))
                return false;

        return in_charset(id, ALPHANUMERICAL);
}

