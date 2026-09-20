/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include <netinet/in.h>

#include "forward.h"

union in_addr_union {
        struct in_addr in;
        struct in6_addr in6;
        uint8_t bytes[CONST_MAX(sizeof(struct in_addr), sizeof(struct in6_addr))];
};

struct in_addr_data {
        int family;
        union in_addr_union address;
};

bool in4_addr_is_null(const struct in_addr *a) _pure_;
bool in6_addr_is_null(const struct in6_addr *a) _pure_;
int in_addr_is_null(int family, const union in_addr_union *u) _pure_;
static inline bool in_addr_is_set(int family, const union in_addr_union *u) {
        return in_addr_is_null(family, u) == 0;
}

const char* typesafe_inet_ntop(int family, const union in_addr_union *a, char *buf, size_t len);
const char* typesafe_inet_ntop6(const struct in6_addr *a, char *buf, size_t len);

/* Note: the lifetime of the compound literal is the immediately surrounding block,
 * see C11 §6.5.2.5, and
 * https://stackoverflow.com/questions/34880638/compound-literal-lifetime-and-if-blocks */
#define IN_ADDR_MAX CONST_MAX(INET_ADDRSTRLEN, INET6_ADDRSTRLEN)
#define IN_ADDR_TO_STRING(family, addr) typesafe_inet_ntop(family, addr, (char[IN_ADDR_MAX]){}, IN_ADDR_MAX)
#define IN6_ADDR_TO_STRING(addr) typesafe_inet_ntop6(addr, (char[INET6_ADDRSTRLEN]){}, INET6_ADDRSTRLEN)

#define PREFIX_SUFFIX_MAX (1 + DECIMAL_STR_MAX(unsigned))

typedef enum InAddrPrefixLenMode {
        PREFIXLEN_FULL,   /* Default to prefixlen of address size, 32 for IPv4 or 128 for IPv6, if not specified. */
        PREFIXLEN_REFUSE, /* Fail with -ENOANO if prefixlen is not specified. */
} InAddrPrefixLenMode;

#define FAMILY_ADDRESS_SIZE_SAFE(f)                                     \
        ({                                                              \
                int _f = (f);                                           \
                _f == AF_INET ? sizeof(struct in_addr) :                \
                _f == AF_INET6 ? sizeof(struct in6_addr) : 0;           \
        })

/* Workaround for clang, explicitly specify the maximum-size element here.
 * See also oss-fuzz#11344. */
#define IN_ADDR_NULL ((union in_addr_union) { .in6 = {} })

extern const struct hash_ops in_addr_data_hash_ops;
extern const struct hash_ops in_addr_data_hash_ops_free;
extern const struct hash_ops in6_addr_hash_ops;
extern const struct hash_ops in6_addr_hash_ops_free;

#define IPV4_ADDRESS_FMT_STR     "%u.%u.%u.%u"
#define IPV4_ADDRESS_FMT_VAL(address)              \
        be32toh((address).s_addr) >> 24,           \
        (be32toh((address).s_addr) >> 16) & 0xFFu, \
        (be32toh((address).s_addr) >> 8) & 0xFFu,  \
        be32toh((address).s_addr) & 0xFFu
