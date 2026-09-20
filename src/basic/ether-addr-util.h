/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include <linux/if_infiniband.h>
#include <netinet/in.h>
#include <net/ethernet.h>

#include "forward.h"

struct hw_addr_data {
        size_t length;
        union {
                struct ether_addr ether;
                uint8_t infiniband[INFINIBAND_ALEN];
                struct in_addr in;
                struct in6_addr in6;
                uint8_t bytes[HW_ADDR_MAX_SIZE];
        };
};

typedef enum HardwareAddressToStringFlags {
        HW_ADDR_TO_STRING_NO_COLON = 1 << 0,
} HardwareAddressToStringFlags;

#define HW_ADDR_TO_STRING_MAX (3*HW_ADDR_MAX_SIZE)

#define HW_ADDR_NULL ((const struct hw_addr_data){})

bool hw_addr_is_null(const struct hw_addr_data *addr) _pure_;

extern const struct hash_ops hw_addr_hash_ops;
extern const struct hash_ops hw_addr_hash_ops_free;

#define ETHER_ADDR_FORMAT_STR "%02X%02X%02X%02X%02X%02X"
#define ETHER_ADDR_FORMAT_VAL(x) (x).ether_addr_octet[0], (x).ether_addr_octet[1], (x).ether_addr_octet[2], (x).ether_addr_octet[3], (x).ether_addr_octet[4], (x).ether_addr_octet[5]

#define ETHER_ADDR_TO_STRING_MAX (3*6)

int ether_addr_compare(const struct ether_addr *a, const struct ether_addr *b);
static inline bool ether_addr_equal(const struct ether_addr *a, const struct ether_addr *b) {
        return ether_addr_compare(a, b) == 0;
}

#define ETHER_ADDR_NULL ((const struct ether_addr){})

static inline bool ether_addr_is_null(const struct ether_addr *addr) {
        return ether_addr_equal(addr, &ETHER_ADDR_NULL);
}

extern const struct hash_ops ether_addr_hash_ops;
extern const struct hash_ops ether_addr_hash_ops_free;
