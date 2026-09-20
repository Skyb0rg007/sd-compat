/* SPDX-License-Identifier: LGPL-2.1-or-later */


#include "capability-list.h"
#include "capability-util.h"
#include "capability-to-name.inc"

const char* capability_to_name(int id) {
        if (id < 0)
                return NULL;
        if ((unsigned) id >= capability_list_length())
                return NULL;

        return capability_names[id];
}

/* This is the number of capability names we are *compiled* with. For the max capability number of the
 * currently-running kernel, use cap_last_cap(). Note that this one returns the size of the array, i.e. one
 * value larger than the last known capability. This is different from cap_last_cap() which returns the
 * highest supported capability. Hence with everyone agreeing on the same capabilities list, this function
 * will return one higher than cap_last_cap(). */
unsigned capability_list_length(void) {
        return MIN((unsigned) ELEMENTSOF(capability_names), (unsigned) (CAP_LIMIT + 1));
}

