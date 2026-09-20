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
char* hw_addr_to_string_full(
                const struct hw_addr_data *addr,
                HardwareAddressToStringFlags flags,
                char buffer[static HW_ADDR_TO_STRING_MAX]);

/* Note: the lifetime of the compound literal is the immediately surrounding block,
 * see C11 §6.5.2.5, and
 * https://stackoverflow.com/questions/34880638/compound-literal-lifetime-and-if-blocks */
#define HW_ADDR_TO_STR_FULL(hw_addr, flags) hw_addr_to_string_full((hw_addr), flags, (char[HW_ADDR_TO_STRING_MAX]){})
#define HW_ADDR_TO_STR(hw_addr) HW_ADDR_TO_STR_FULL(hw_addr, 0)

#define HW_ADDR_NULL ((const struct hw_addr_data){})

bool hw_addr_is_null(const struct hw_addr_data *addr) _pure_;

extern const struct hash_ops hw_addr_hash_ops;
extern const struct hash_ops hw_addr_hash_ops_free;

#define ETHER_ADDR_FORMAT_STR "%02X%02X%02X%02X%02X%02X"
#define ETHER_ADDR_FORMAT_VAL(x) (x).ether_addr_octet[0], (x).ether_addr_octet[1], (x).ether_addr_octet[2], (x).ether_addr_octet[3], (x).ether_addr_octet[4], (x).ether_addr_octet[5]

#define ETHER_ADDR_TO_STRING_MAX (3*6)
char* ether_addr_to_string(const struct ether_addr *addr, char buffer[ETHER_ADDR_TO_STRING_MAX]);
/* Use only as function argument, never stand-alone! */
#define ETHER_ADDR_TO_STR(addr) ether_addr_to_string((addr), (char[ETHER_ADDR_TO_STRING_MAX]){})

int ether_addr_compare(const struct ether_addr *a, const struct ether_addr *b);
static inline bool ether_addr_equal(const struct ether_addr *a, const struct ether_addr *b) {
        return ether_addr_compare(a, b) == 0;
}

#define ETHER_ADDR_NULL ((const struct ether_addr){})

static inline bool ether_addr_is_null(const struct ether_addr *addr) {
        return ether_addr_equal(addr, &ETHER_ADDR_NULL);
}

bool ether_addr_is_broadcast(const struct ether_addr *addr) _pure_;

extern const struct hash_ops ether_addr_hash_ops;
extern const struct hash_ops ether_addr_hash_ops_free;

