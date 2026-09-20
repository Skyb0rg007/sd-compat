/* SPDX-License-Identifier: LGPL-2.1-or-later */


#include "ether-addr-util.h"
#include "string-util.h"

bool hw_addr_is_null(const struct hw_addr_data *addr) {
        assert(addr);
        return addr->length == 0 || memeqzero(addr->bytes, addr->length);
}

int ether_addr_compare(const struct ether_addr *a, const struct ether_addr *b) {
        assert(a);
        assert(b);

        return memcmp(a, b, ETH_ALEN);
}

