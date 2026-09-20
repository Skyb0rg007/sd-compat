/* SPDX-License-Identifier: LGPL-2.1-or-later */


static bool dlopen_blocked = false;

void block_dlopen(void) {
        dlopen_blocked = true;
}

