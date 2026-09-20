/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <arpa/inet.h>

#include "in-addr-util.h"
#include "string-util.h"

bool in4_addr_is_null(const struct in_addr *a) {
        assert(a);

        return a->s_addr == 0;
}

bool in6_addr_is_null(const struct in6_addr *a) {
        assert(a);

        return eqzero(a->s6_addr32);
}

int in_addr_is_null(int family, const union in_addr_union *u) {
        assert(u);

        if (family == AF_INET)
                return in4_addr_is_null(&u->in);

        if (family == AF_INET6)
                return in6_addr_is_null(&u->in6);

        return -EAFNOSUPPORT;
}

/*
 * Calculates the nth prefix of size prefixlen starting from the address denoted by u.
 *
 * On success 0 will be returned and the calculated prefix will be available in
 * u. In case the calculation cannot be performed (invalid prefix length,
 * overflows would occur) -ERANGE is returned. If the address family given isn't
 * supported -EAFNOSUPPORT will be returned.
 *
 * Examples:
 *   - in_addr_prefix_nth(AF_INET, 192.168.0.0, 24, 2), returns 0, writes 192.168.2.0 to u
 *   - in_addr_prefix_nth(AF_INET, 192.168.0.0, 24, 0), returns 0, no data written
 *   - in_addr_prefix_nth(AF_INET, 255.255.255.0, 24, 1), returns -ERANGE, no data written
 *   - in_addr_prefix_nth(AF_INET, 255.255.255.0, 0, 1), returns -ERANGE, no data written
 *   - in_addr_prefix_nth(AF_INET6, 2001:db8, 64, 0xff00) returns 0, writes 2001:0db8:0000:ff00:: to u
 */
const char* typesafe_inet_ntop(int family, const union in_addr_union *a, char *buf, size_t len) {
        return inet_ntop(family, a, buf, len);
}

const char* typesafe_inet_ntop6(const struct in6_addr *a, char *buf, size_t len) {
        return inet_ntop(AF_INET6, a, buf, len);
}

/* Calculate an IPv4 netmask from prefix length, for example /8 -> 255.0.0.0. */
/* Calculate an IPv6 netmask from prefix length, for example /16 -> ffff::. */
/* Calculate an IPv4 or IPv6 netmask from prefix length, for example /8 -> 255.0.0.0 or /16 -> ffff::. */
