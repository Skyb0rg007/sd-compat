/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include <linux/if_packet.h>
#include <linux/netlink.h>
#include <linux/vm_sockets.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "forward.h"
#include "memory-util.h"
#include "missing-network.h"

union sockaddr_union {
        /* The minimal, abstract version */
        struct sockaddr sa;

        /* The libc provided version that allocates "enough room" for every protocol */
        struct sockaddr_storage storage;

        /* Protocol-specific implementations */
        struct sockaddr_in in;
        struct sockaddr_in6 in6;
        struct sockaddr_un un;
        struct sockaddr_nl nl;
        struct sockaddr_ll ll;
        struct sockaddr_vm vm;

        /* Ensure there is enough space to store an arbitrary hardware address, e.g. Infiniband */
        uint8_t ll_buffer[offsetof(struct sockaddr_ll, sll_addr) + HW_ADDR_MAX_SIZE];

        /* Ensure there is enough space after the AF_UNIX sun_path for one more NUL byte, just to be sure that the path
         * component is always followed by at least one NUL byte. */
        uint8_t un_buffer[sizeof(struct sockaddr_un) + 1];
};

#define SUN_PATH_LEN (sizeof(((struct sockaddr_un){}).sun_path))

typedef struct SocketAddress {
        union sockaddr_union sockaddr;

        /* We store the size here explicitly due to the weird
         * sockaddr_un semantics for abstract sockets */
        socklen_t size;

        /* Socket type, i.e. SOCK_STREAM, SOCK_DGRAM, ... */
        int type;

        /* Socket protocol, IPPROTO_xxx, usually 0, except for netlink */
        int protocol;
} SocketAddress;

#define socket_address_family(a) ((a)->sockaddr.sa.sa_family)

DECLARE_STRING_TABLE_LOOKUP(socket_address_type, int);

int sockaddr_un_unlink(const struct sockaddr_un *sa);

int sockaddr_port(const struct sockaddr *_sa, unsigned *port);

int sockaddr_pretty(const struct sockaddr *_sa, socklen_t salen, bool translate_ipv6, bool include_port, char **ret);

int fd_set_sndbuf(int fd, size_t n, bool increase);
static inline int fd_inc_sndbuf(int fd, size_t n) {
        return fd_set_sndbuf(fd, n, true);
}
int fd_set_rcvbuf(int fd, size_t n, bool increase);
static inline int fd_increase_rxbuf(int fd, size_t n) {
        return fd_set_rcvbuf(fd, n, true);
}

int getpeercred(int fd, struct ucred *ucred);
int getpeersec(int fd, char **ret);
int getpeergroups(int fd, gid_t **ret);
int getpeerpidfd(int fd);

ssize_t send_one_fd_iov_sa(
                int transport_fd,
                int fd,
                const struct iovec *iov, size_t iovlen,
                const struct sockaddr *sa, socklen_t len,
                int flags);
#define send_one_fd_iov(transport_fd, fd, iov, iovlen, flags) send_one_fd_iov_sa(transport_fd, fd, iov, iovlen, NULL, 0, flags)
#define send_one_fd(transport_fd, fd, flags) send_one_fd_iov_sa(transport_fd, fd, NULL, 0, NULL, 0, flags)
ssize_t receive_one_fd_iov(int transport_fd, struct iovec *iov, size_t iovlen, int flags, int *ret_fd);

#define CMSG_FOREACH(cmsg, mh)                                          \
        for ((cmsg) = CMSG_FIRSTHDR(mh); (cmsg); (cmsg) = CMSG_NXTHDR((mh), (cmsg)))

/* Returns the cmsghdr's data pointer, but safely cast to the specified type. Does two alignment checks: one
 * at compile time, that the requested type has a smaller or same alignment as 'struct cmsghdr', and one
 * during runtime, that the actual pointer matches the alignment too. This is supposed to catch cases such as
 * 'struct timeval' is embedded into 'struct cmsghdr' on architectures where the alignment of the former is 8
 * bytes (because of a 64-bit time_t), but of the latter is 4 bytes (because size_t is 32 bits), such as
 * riscv32. */
#define CMSG_TYPED_DATA(cmsg, type)                                     \
        ({                                                              \
                struct cmsghdr *_cmsg = (cmsg);                         \
                assert_cc(alignof(type) <= alignof(struct cmsghdr));    \
                _cmsg ? CAST_ALIGN_PTR(type, CMSG_DATA(_cmsg)) : (type*) NULL; \
        })

struct cmsghdr* cmsg_find(struct msghdr *mh, int level, int type, socklen_t length);

/* Type-safe, dereferencing version of cmsg_find() */
#define CMSG_FIND_DATA(mh, level, type, ctype)                          \
        CMSG_TYPED_DATA(cmsg_find(mh, level, type, CMSG_LEN(sizeof(ctype))), ctype)

/* Resolves to a type that can carry cmsghdr structures. Make sure things are properly aligned, i.e. the type
 * itself is placed properly in memory and the size is also aligned to what's appropriate for "cmsghdr"
 * structures. */
#define CMSG_BUFFER_TYPE(size)                                          \
        union {                                                         \
                struct cmsghdr cmsghdr;                                 \
                uint8_t buf[size];                                      \
                uint8_t align_check[(size) >= CMSG_SPACE(0) &&          \
                                    (size) == CMSG_ALIGN(size) ? 1 : -1]; \
        }

size_t sockaddr_un_len(const struct sockaddr_un *sa);
size_t sockaddr_len(const union sockaddr_union *sa);

int sockaddr_un_set_path(struct sockaddr_un *ret, const char *path);

static inline int setsockopt_int(int fd, int level, int optname, int value) {
        if (setsockopt(fd, level, optname, &value, sizeof(value)) < 0)
                return -errno;

        return 0;
}

/* glibc duplicates timespec/timeval on certain 32-bit arches, once in 32-bit and once in 64-bit.
 * See __convert_scm_timestamps() in glibc source code. Hence, we need additional buffer space for them
 * to prevent truncating control msg (recvmsg() MSG_CTRUNC). */
#define CMSG_SPACE_TIMEVAL                                              \
        (CMSG_SPACE(sizeof(struct timeval)) + CMSG_SPACE(2 * sizeof(uint64_t)))
#define CMSG_SPACE_TIMESPEC                                             \
        (CMSG_SPACE(sizeof(struct timespec)) + CMSG_SPACE(2 * sizeof(uint64_t)))

ssize_t recvmsg_safe(int sockfd, struct msghdr *msg, int flags);

int socket_get_family(int fd);

/* an initializer for struct ucred that initialized all fields to the invalid value appropriate for each */
#define UCRED_INVALID { .pid = 0, .uid = UID_INVALID, .gid = GID_INVALID }

int connect_unix_path(int fd, int dir_fd, const char *path);

int vsock_parse_port(const char *s, unsigned *ret);
int vsock_parse_cid(const char *s, unsigned *ret);

/* Parses AF_UNIX and AF_VSOCK addresses. AF_INET[6] require some netlink calls, so it cannot be in
 * src/basic/ and is done from 'socket_local_address from src/shared/. Return -EPROTO in case of
 * protocol mismatch. */
int socket_address_parse_unix(SocketAddress *ret_address, const char *s);
int socket_address_parse_vsock(SocketAddress *ret_address, const char *s);

/* libc's SOMAXCONN is defined to 128 or 4096 (at least on glibc). But actually, the value can be much
 * larger. In our codebase we want to set it to the max usually, since nowadays socket memory is properly
 * tracked by memcg, and hence we don't need to enforce extra limits here. Moreover, the kernel caps it to
 * /proc/sys/net/core/somaxconn anyway, thus by setting this to unbounded we just make that sysctl file
 * authoritative. */
#define SOMAXCONN_DELUXE INT_MAX

void cmsg_close_all(struct msghdr *mh);

int socket_xattr_supported(void);
