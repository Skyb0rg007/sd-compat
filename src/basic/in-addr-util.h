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

bool in4_addr_is_multicast(const struct in_addr *a) _pure_;
bool in6_addr_is_multicast(const struct in6_addr *a) _pure_;
int in_addr_is_multicast(int family, const union in_addr_union *u) _pure_;

bool in4_addr_is_link_local(const struct in_addr *a) _pure_;
bool in4_addr_is_link_local_dynamic(const struct in_addr *a) _pure_;
bool in6_addr_is_link_local(const struct in6_addr *a) _pure_;
int in_addr_is_link_local(int family, const union in_addr_union *u) _pure_;
bool in6_addr_is_link_local_all_nodes(const struct in6_addr *a) _pure_;

bool in4_addr_is_localhost(const struct in_addr *a) _pure_;
int in_addr_is_localhost(int family, const union in_addr_union *u) _pure_;
int in_addr_is_localhost_one(int family, const union in_addr_union *u) _pure_;

bool in4_addr_is_local_multicast(const struct in_addr *a) _pure_;
bool in4_addr_is_non_local(const struct in_addr *a) _pure_;
bool in6_addr_is_ipv4_mapped_address(const struct in6_addr *a) _pure_;

bool in4_addr_equal(const struct in_addr *a, const struct in_addr *b) _pure_;
bool in6_addr_equal(const struct in6_addr *a, const struct in6_addr *b) _pure_;
int in_addr_equal(int family, const union in_addr_union *a, const union in_addr_union *b) _pure_;
bool in4_addr_prefix_intersect(
                const struct in_addr *a,
                unsigned aprefixlen,
                const struct in_addr *b,
                unsigned bprefixlen) _pure_;
bool in6_addr_prefix_intersect(
                const struct in6_addr *a,
                unsigned aprefixlen,
                const struct in6_addr *b,
                unsigned bprefixlen) _pure_;
int in_addr_prefix_intersect(
                int family,
                const union in_addr_union *a,
                unsigned aprefixlen,
                const union in_addr_union *b,
                unsigned bprefixlen) _pure_;

const char* typesafe_inet_ntop(int family, const union in_addr_union *a, char *buf, size_t len);
const char* typesafe_inet_ntop4(const struct in_addr *a, char *buf, size_t len);
const char* typesafe_inet_ntop6(const struct in6_addr *a, char *buf, size_t len);

/* Note: the lifetime of the compound literal is the immediately surrounding block,
 * see C11 §6.5.2.5, and
 * https://stackoverflow.com/questions/34880638/compound-literal-lifetime-and-if-blocks */
#define IN_ADDR_MAX CONST_MAX(INET_ADDRSTRLEN, INET6_ADDRSTRLEN)
#define IN_ADDR_TO_STRING(family, addr) typesafe_inet_ntop(family, addr, (char[IN_ADDR_MAX]){}, IN_ADDR_MAX)
#define IN4_ADDR_TO_STRING(addr) typesafe_inet_ntop4(addr, (char[INET_ADDRSTRLEN]){}, INET_ADDRSTRLEN)
#define IN6_ADDR_TO_STRING(addr) typesafe_inet_ntop6(addr, (char[INET6_ADDRSTRLEN]){}, INET6_ADDRSTRLEN)

int in_addr_prefix_to_string(
                int family,
                const union in_addr_union *u,
                unsigned prefixlen,
                char *buf,
                size_t buf_len);

static inline const char* _in_addr_prefix_to_string(
                int family,
                const union in_addr_union *u,
                unsigned prefixlen,
                char *buf,
                size_t buf_len) {
        /* We assume that this is called with an appropriately sized buffer and can never fail. */
        assert_se(in_addr_prefix_to_string(family, u, prefixlen, buf, buf_len) == 0);
        return buf;
}
static inline const char* _in4_addr_prefix_to_string(const struct in_addr *a, unsigned prefixlen, char *buf, size_t buf_len) {
        return _in_addr_prefix_to_string(AF_INET, (const union in_addr_union *) a, prefixlen, buf, buf_len);
}
static inline const char* _in6_addr_prefix_to_string(const struct in6_addr *a, unsigned prefixlen, char *buf, size_t buf_len) {
        return _in_addr_prefix_to_string(AF_INET6, (const union in_addr_union *) a, prefixlen, buf, buf_len);
}

#define PREFIX_SUFFIX_MAX (1 + DECIMAL_STR_MAX(unsigned))
#define IN_ADDR_PREFIX_TO_STRING(family, addr, prefixlen) \
        _in_addr_prefix_to_string(family, addr, prefixlen, (char[IN_ADDR_MAX + PREFIX_SUFFIX_MAX]){}, IN_ADDR_MAX + PREFIX_SUFFIX_MAX)
#define IN4_ADDR_PREFIX_TO_STRING(addr, prefixlen) \
        _in4_addr_prefix_to_string(addr, prefixlen, (char[INET_ADDRSTRLEN + PREFIX_SUFFIX_MAX]){}, INET_ADDRSTRLEN + PREFIX_SUFFIX_MAX)
#define IN6_ADDR_PREFIX_TO_STRING(addr, prefixlen) \
        _in6_addr_prefix_to_string(addr, prefixlen, (char[INET6_ADDRSTRLEN + PREFIX_SUFFIX_MAX]){}, INET6_ADDRSTRLEN + PREFIX_SUFFIX_MAX)

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
