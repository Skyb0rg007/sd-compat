/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <sys/socket.h>

#include "string-util.h"

int af_from_ipv4_ipv6(const char *af) {
        return streq_ptr(af, "ipv4") ? AF_INET :
                streq_ptr(af, "ipv6") ? AF_INET6 : AF_UNSPEC;
}
