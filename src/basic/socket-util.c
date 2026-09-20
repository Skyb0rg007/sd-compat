/* SPDX-License-Identifier: LGPL-2.1-or-later */


#include <unistd.h>

#include "escape.h"
#include "fd-util.h"
#include "format-ifname.h"
#include "in-addr-util.h"
#include "log.h"
#include "parse-util.h"
#include "process-util.h"
#include "socket-util.h"
#include "string-table.h"
#include "strv.h"
#include "user-util.h"
#include "xattr-util.h"

#if ENABLE_IDN
#  define IDN_FLAGS NI_IDN
#else
#  define IDN_FLAGS 0
#endif

static const char* const socket_address_type_table[] = {
        [SOCK_STREAM] =    "Stream",
        [SOCK_DGRAM] =     "Datagram",
        [SOCK_RAW] =       "Raw",
        [SOCK_RDM] =       "ReliableDatagram",
        [SOCK_SEQPACKET] = "SequentialPacket",
        [SOCK_DCCP] =      "DatagramCongestionControl",
};

DEFINE_STRING_TABLE_LOOKUP(socket_address_type, int);

int sockaddr_port(const struct sockaddr *_sa, unsigned *ret_port) {
        const union sockaddr_union *sa = (const union sockaddr_union*) _sa;

        /* Note, this returns the port as 'unsigned' rather than 'uint16_t', as AF_VSOCK knows larger ports */

        assert(sa);
        assert(ret_port);

        switch (sa->sa.sa_family) {

        case AF_INET:
                *ret_port = be16toh(sa->in.sin_port);
                return 0;

        case AF_INET6:
                *ret_port = be16toh(sa->in6.sin6_port);
                return 0;

        case AF_VSOCK:
                *ret_port = sa->vm.svm_port;
                return 0;

        default:
                return -EAFNOSUPPORT;
        }
}

int sockaddr_pretty(
                const struct sockaddr *_sa,
                socklen_t salen,
                bool translate_ipv6,
                bool include_port,
                char **ret) {

        union sockaddr_union *sa = (union sockaddr_union*) _sa;
        char *p;
        int r;

        assert(sa);
        assert(salen >= sizeof(sa->sa.sa_family));
        assert(ret);

        switch (sa->sa.sa_family) {

        case AF_INET: {
                uint32_t a;

                a = be32toh(sa->in.sin_addr.s_addr);

                if (include_port)
                        r = asprintf(&p,
                                     "%u.%u.%u.%u:%u",
                                     a >> 24, (a >> 16) & 0xFF, (a >> 8) & 0xFF, a & 0xFF,
                                     be16toh(sa->in.sin_port));
                else
                        r = asprintf(&p,
                                     "%u.%u.%u.%u",
                                     a >> 24, (a >> 16) & 0xFF, (a >> 8) & 0xFF, a & 0xFF);
                if (r < 0)
                        return -ENOMEM;
                break;
        }

        case AF_INET6: {
                static const unsigned char ipv4_prefix[] = {
                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF
                };

                if (translate_ipv6 &&
                    memcmp(&sa->in6.sin6_addr, ipv4_prefix, sizeof(ipv4_prefix)) == 0) {
                        const uint8_t *a = sa->in6.sin6_addr.s6_addr+12;
                        if (include_port)
                                r = asprintf(&p,
                                             "%u.%u.%u.%u:%u",
                                             a[0], a[1], a[2], a[3],
                                             be16toh(sa->in6.sin6_port));
                        else
                                r = asprintf(&p,
                                             "%u.%u.%u.%u",
                                             a[0], a[1], a[2], a[3]);
                        if (r < 0)
                                return -ENOMEM;
                } else {
                        const char *a = IN6_ADDR_TO_STRING(&sa->in6.sin6_addr);

                        if (include_port) {
                                if (asprintf(&p,
                                             "[%s]:%u%s%s",
                                             a,
                                             be16toh(sa->in6.sin6_port),
                                             sa->in6.sin6_scope_id != 0 ? "%" : "",
                                             FORMAT_IFNAME_FULL(sa->in6.sin6_scope_id, FORMAT_IFNAME_IFINDEX)) < 0)
                                        return -ENOMEM;
                        } else {
                                if (sa->in6.sin6_scope_id != 0)
                                        p = strjoin(a, "%", FORMAT_IFNAME_FULL(sa->in6.sin6_scope_id, FORMAT_IFNAME_IFINDEX));
                                else
                                        p = strdup(a);
                                if (!p)
                                        return -ENOMEM;
                        }
                }

                break;
        }

        case AF_UNIX:
                if (salen <= offsetof(struct sockaddr_un, sun_path) ||
                    (sa->un.sun_path[0] == 0 && salen == offsetof(struct sockaddr_un, sun_path) + 1))
                        /* The name must have at least one character (and the leading NUL does not count) */
                        p = strdup("<unnamed>");
                else {
                        /* Note that we calculate the path pointer here through the .un_buffer[] field, in order to
                         * outtrick bounds checking tools such as ubsan, which are too smart for their own good: on
                         * Linux the kernel may return sun_path[] data one byte longer than the declared size of the
                         * field. */
                        char *path = (char*) sa->un_buffer + offsetof(struct sockaddr_un, sun_path);
                        size_t path_len = salen - offsetof(struct sockaddr_un, sun_path);

                        if (path[0] == 0) {
                                /* Abstract socket. When parsing address information from, we
                                 * explicitly reject overly long paths and paths with embedded NULs.
                                 * But we might get such a socket from the outside. Let's return
                                 * something meaningful and printable in this case. */

                                _cleanup_free_ char *e = NULL;

                                e = cescape_length(path + 1, path_len - 1);
                                if (!e)
                                        return -ENOMEM;

                                p = strjoin("@", e);
                        } else {
                                if (path[path_len - 1] == '\0')
                                        /* We expect a terminating NUL and don't print it */
                                        path_len--;

                                p = cescape_length(path, path_len);
                        }
                }
                if (!p)
                        return -ENOMEM;

                break;

        case AF_VSOCK:
                if (include_port) {
                        if (sa->vm.svm_cid == VMADDR_CID_ANY)
                                r = asprintf(&p, "vsock::%u", sa->vm.svm_port);
                        else
                                r = asprintf(&p, "vsock:%u:%u", sa->vm.svm_cid, sa->vm.svm_port);
                } else
                        r = asprintf(&p, "vsock:%u", sa->vm.svm_cid);
                if (r < 0)
                        return -ENOMEM;
                break;

        default:
                return -EOPNOTSUPP;
        }

        *ret = p;
        return 0;
}

int fd_set_sndbuf(int fd, size_t n, bool increase) {
        int r, value;
        socklen_t l = sizeof(value);

        if (n > INT_MAX)
                return -ERANGE;

        r = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &value, &l);
        if (r >= 0 && l == sizeof(value) && increase ? (size_t) value >= n*2 : (size_t) value == n*2)
                return 0;

        /* First, try to set the buffer size with SO_SNDBUF. */
        r = setsockopt_int(fd, SOL_SOCKET, SO_SNDBUF, n);
        if (r < 0)
                return r;

        /* SO_SNDBUF above may set to the kernel limit, instead of the requested size.
         * So, we need to check the actual buffer size here. */
        l = sizeof(value);
        r = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &value, &l);
        if (r >= 0 && l == sizeof(value) && increase ? (size_t) value >= n*2 : (size_t) value == n*2)
                return 1;

        /* If we have the privileges we will ignore the kernel limit. */
        r = setsockopt_int(fd, SOL_SOCKET, SO_SNDBUFFORCE, n);
        if (r < 0)
                return r;

        return 1;
}

int fd_set_rcvbuf(int fd, size_t n, bool increase) {
        int r, value;
        socklen_t l = sizeof(value);

        if (n > INT_MAX)
                return -ERANGE;

        r = getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &value, &l);
        if (r >= 0 && l == sizeof(value) && increase ? (size_t) value >= n*2 : (size_t) value == n*2)
                return 0;

        /* First, try to set the buffer size with SO_RCVBUF. */
        r = setsockopt_int(fd, SOL_SOCKET, SO_RCVBUF, n);
        if (r < 0)
                return r;

        /* SO_RCVBUF above may set to the kernel limit, instead of the requested size.
         * So, we need to check the actual buffer size here. */
        l = sizeof(value);
        r = getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &value, &l);
        if (r >= 0 && l == sizeof(value) && increase ? (size_t) value >= n*2 : (size_t) value == n*2)
                return 1;

        /* If we have the privileges we will ignore the kernel limit. */
        r = setsockopt_int(fd, SOL_SOCKET, SO_RCVBUFFORCE, n);
        if (r < 0)
                return r;

        return 1;
}

int getpeercred(int fd, struct ucred *ucred) {
        socklen_t n = sizeof(struct ucred);
        struct ucred u;

        assert(fd >= 0);
        assert(ucred);

        if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &u, &n) < 0)
                return -errno;

        if (n != sizeof(struct ucred))
                return -EIO;

        /* Check if the data is actually useful and not suppressed due to namespacing issues */
        if (!pid_is_valid(u.pid))
                return -ENODATA;

        /* Note that we don't check UID/GID here, as namespace translation works differently there: instead of
         * receiving in "invalid" user/group we get the overflow UID/GID. */

        *ucred = u;
        return 0;
}

int getpeersec(int fd, char **ret) {
        _cleanup_free_ char *s = NULL;
        socklen_t n = 64;

        assert(fd >= 0);
        assert(ret);

        for (;;) {
                s = new0(char, n+1);
                if (!s)
                        return -ENOMEM;

                if (getsockopt(fd, SOL_SOCKET, SO_PEERSEC, s, &n) >= 0) {
                        s[n] = 0;
                        break;
                }

                if (errno != ERANGE)
                        return -errno;

                s = mfree(s);
        }

        if (isempty(s))
                return -EOPNOTSUPP;

        *ret = TAKE_PTR(s);

        return 0;
}

int getpeergroups(int fd, gid_t **ret) {
        socklen_t n = sizeof(gid_t) * 64U;
        _cleanup_free_ gid_t *d = NULL;

        assert(fd >= 0);
        assert(ret);

        int ngroups_max = sysconf_ngroups_max();
        if (ngroups_max > 0)
                n = MAX(n, sizeof(gid_t) * (socklen_t) ngroups_max);

        for (;;) {
                d = malloc(n);
                if (!d)
                        return -ENOMEM;

                if (getsockopt(fd, SOL_SOCKET, SO_PEERGROUPS, d, &n) >= 0)
                        break;

                if (errno != ERANGE)
                        return -errno;

                d = mfree(d);
        }

        assert_se(n % sizeof(gid_t) == 0);
        n /= sizeof(gid_t);

        if (n > INT_MAX)
                return -E2BIG;

        *ret = TAKE_PTR(d);

        return (int) n;
}

int getpeerpidfd(int fd) {
        socklen_t n = sizeof(int);
        int pidfd = -EBADF;

        assert(fd >= 0);

        if (getsockopt(fd, SOL_SOCKET, SO_PEERPIDFD, &pidfd, &n) < 0)
                return -errno;

        if (n != sizeof(int))
                return -EIO;

        return pidfd;
}

ssize_t send_one_fd_iov_sa(
                int transport_fd,
                int fd,
                const struct iovec *iov, size_t iovlen,
                const struct sockaddr *sa, socklen_t len,
                int flags) {

        CMSG_BUFFER_TYPE(CMSG_SPACE(sizeof(int))) control = {};
        struct msghdr mh = {
                .msg_name = (struct sockaddr*) sa,
                .msg_namelen = len,
                .msg_iov = (struct iovec *)iov,
                .msg_iovlen = iovlen,
        };
        ssize_t k;

        assert(transport_fd >= 0);

        /*
         * We need either an FD or data to send.
         * If there's nothing, return an error.
         */
        if (fd < 0 && !iov)
                return -EINVAL;

        if (fd >= 0) {
                struct cmsghdr *cmsg;

                mh.msg_control = &control;
                mh.msg_controllen = sizeof(control);

                cmsg = CMSG_FIRSTHDR(&mh);
                cmsg->cmsg_level = SOL_SOCKET;
                cmsg->cmsg_type = SCM_RIGHTS;
                cmsg->cmsg_len = CMSG_LEN(sizeof(int));
                memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));
        }
        k = sendmsg(transport_fd, &mh, MSG_NOSIGNAL | flags);
        if (k < 0)
                return (ssize_t) -errno;

        return k;
}

ssize_t receive_one_fd_iov(
                int transport_fd,
                struct iovec *iov, size_t iovlen,
                int flags,
                int *ret_fd) {

        CMSG_BUFFER_TYPE(CMSG_SPACE(sizeof(int))) control;
        struct msghdr mh = {
                .msg_control = &control,
                .msg_controllen = sizeof(control),
                .msg_iov = iov,
                .msg_iovlen = iovlen,
        };
        struct cmsghdr *found;
        ssize_t k;

        assert(transport_fd >= 0);
        assert(ret_fd);

        /*
         * Receive a single FD via @transport_fd. We don't care for
         * the transport-type. We retrieve a single FD at most, so for
         * packet-based transports, the caller must ensure to send
         * only a single FD per packet.  This is best used in
         * combination with send_one_fd().
         */

        k = recvmsg_safe(transport_fd, &mh, MSG_CMSG_CLOEXEC | flags);
        if (k < 0)
                return k;

        found = cmsg_find(&mh, SOL_SOCKET, SCM_RIGHTS, CMSG_LEN(sizeof(int)));
        if (!found) {
                cmsg_close_all(&mh);

                /* If didn't receive an FD or any data, return an error. */
                if (k == 0)
                        return -EIO;
        }

        if (found)
                *ret_fd = *CMSG_TYPED_DATA(found, int);
        else
                *ret_fd = -EBADF;

        return k;
}

/* Put a limit on how many times will attempt to call accept4(). We loop
 * only on "transient" errors, but let's make sure we don't loop forever. */
#define MAX_FLUSH_ITERATIONS 1024

struct cmsghdr* cmsg_find(struct msghdr *mh, int level, int type, socklen_t length) {
        struct cmsghdr *cmsg;

        assert(mh);

        CMSG_FOREACH(cmsg, mh)
                if (cmsg->cmsg_level == level &&
                    cmsg->cmsg_type == type &&
                    (length == (socklen_t) -1 || length == cmsg->cmsg_len))
                        return cmsg;

        return NULL;
}

size_t sockaddr_un_len(const struct sockaddr_un *sa) {
        /* Covers only file system and abstract AF_UNIX socket addresses, but not unnamed socket addresses. */

        assert(sa->sun_family == AF_UNIX);

        return offsetof(struct sockaddr_un, sun_path) +
                (sa->sun_path[0] == 0 ?
                        1 + strnlen(sa->sun_path+1, sizeof(sa->sun_path)-1) :
                        strnlen(sa->sun_path, sizeof(sa->sun_path))+1);
}

int sockaddr_un_unlink(const struct sockaddr_un *sa) {
        const char *p, * nul;

        assert(sa);

        if (sa->sun_family != AF_UNIX)
                return -EPROTOTYPE;

        if (sa->sun_path[0] == 0) /* Nothing to do for abstract sockets */
                return 0;

        /* The path in .sun_path is not necessarily NUL terminated. Let's fix that. */
        nul = memchr(sa->sun_path, 0, sizeof(sa->sun_path));
        if (nul)
                p = sa->sun_path;
        else
                p = memdupa_suffix0(sa->sun_path, sizeof(sa->sun_path));

        if (unlink(p) < 0)
                return -errno;

        return 1;
}

int sockaddr_un_set_path(struct sockaddr_un *ret, const char *path) {
        size_t l;

        assert(ret);
        assert(path);

        /* Initialize ret->sun_path from the specified argument. This will interpret paths starting with '@' as
         * abstract namespace sockets, and those starting with '/' as regular filesystem sockets. It won't accept
         * anything else (i.e. no relative paths), to avoid ambiguities. Note that this function cannot be used to
         * reference paths in the abstract namespace that include NUL bytes in the name. */

        l = strlen(path);
        if (l < 2)
                return -EINVAL;
        if (!IN_SET(path[0], '/', '@'))
                return -EINVAL;

        /* Don't allow paths larger than the space in sockaddr_un. Note that we are a tiny bit more restrictive than
         * the kernel is: we insist on NUL termination (both for abstract namespace and regular file system socket
         * addresses!), which the kernel doesn't. We do this to reduce chance of incompatibility with other apps that
         * do not expect non-NUL terminated file system path. */
        if (l+1 > sizeof(ret->sun_path))
                return path[0] == '@' ? -EINVAL : -ENAMETOOLONG; /* return a recognizable error if this is
                                                                  * too long to fit into a sockaddr_un, but
                                                                  * is a file system path, and thus might be
                                                                  * connectible via O_PATH indirection. */

        *ret = (struct sockaddr_un) {
                .sun_family = AF_UNIX,
        };

        if (path[0] == '@') {
                /* Abstract namespace socket */
                memcpy(ret->sun_path + 1, path + 1, l); /* copy *with* trailing NUL byte */
                return (int) (offsetof(struct sockaddr_un, sun_path) + l); /* 🔥 *don't* 🔥 include trailing NUL in size */

        } else {
                assert(path[0] == '/');

                /* File system socket */
                memcpy(ret->sun_path, path, l + 1); /* copy *with* trailing NUL byte */
                return (int) (offsetof(struct sockaddr_un, sun_path) + l + 1); /* include trailing NUL in size */
        }
}

ssize_t recvmsg_safe(int sockfd, struct msghdr *msg, int flags) {
        ssize_t n;

        /* A wrapper around recvmsg() that checks for MSG_CTRUNC and MSG_TRUNC, and turns them into an error,
         * in a reasonably safe way, closing any received fds in the error path.
         *
         * Note that unlike our usual coding style this might modify *msg on failure. */

        assert(sockfd >= 0);
        assert(msg);

        n = recvmsg(sockfd, msg, flags);
        if (n < 0)
                return -errno;

        if (FLAGS_SET(msg->msg_flags, MSG_CTRUNC) ||
            (!FLAGS_SET(flags, MSG_PEEK) && FLAGS_SET(msg->msg_flags, MSG_TRUNC))) {
                cmsg_close_all(msg);
                return FLAGS_SET(msg->msg_flags, MSG_CTRUNC) ? -ECHRNG : -EXFULL;
        }

        return n;
}

int socket_get_family(int fd) {
        int af;
        socklen_t sl = sizeof(af);

        if (getsockopt(fd, SOL_SOCKET, SO_DOMAIN, &af, &sl) < 0)
                return -errno;

        if (sl != sizeof(af))
                return -EINVAL;

        return af;
}

static int connect_unix_path_simple(int fd, const char *path) {
        union sockaddr_union sa = {
                .un.sun_family = AF_UNIX,
        };
        size_t l;

        assert(fd >= 0);
        assert(path);

        l = strlen(path);
        assert(l > 0);
        assert(l < sizeof(sa.un.sun_path));

        memcpy(sa.un.sun_path, path, l + 1);
        return RET_NERRNO(connect(fd, &sa.sa, offsetof(struct sockaddr_un, sun_path) + l + 1));
}

static int connect_unix_inode(int fd, int inode_fd) {
        assert(fd >= 0);
        assert(inode_fd >= 0);

        return connect_unix_path_simple(fd, FORMAT_PROC_FD_PATH(inode_fd));
}

int connect_unix_path(int fd, int dir_fd, const char *path) {
        _cleanup_close_ int inode_fd = -EBADF;

        assert(fd >= 0);
        assert(wildcard_fd_is_valid(dir_fd));

        /* Connects to the specified AF_UNIX socket in the file system. Works around the 108 byte size limit
         * in sockaddr_un, by going via O_PATH if needed. This hence works for any kind of path. */

        if (!path) {
                if (dir_fd < 0)
                        return -EISDIR;

                return connect_unix_inode(fd, dir_fd); /* If no path is specified, then dir_fd refers to the socket inode to connect to. */
        }

        /* Refuse zero length path early, to make sure AF_UNIX stack won't mistake this for an abstract
         * namespace path, since first char is NUL */
        if (isempty(path))
                return -EINVAL;

        /* Shortcut for the simple case */
        if (dir_fd == AT_FDCWD && strlen(path) < sizeof_field(struct sockaddr_un, sun_path))
                return connect_unix_path_simple(fd, path);

        /* If dir_fd is specified, then we need to go the indirect O_PATH route, because connectat() does not
         * exist. If the path is too long, we also need to take the indirect route, since we can't fit this
         * into a sockaddr_un directly. */

        if (dir_fd == XAT_FDROOT) {
                _cleanup_free_ char *j = strjoin("/", path);
                if (!j)
                        return -ENOMEM;

                inode_fd = open(j, O_PATH|O_CLOEXEC);
        } else
                inode_fd = openat(dir_fd, path, O_PATH|O_CLOEXEC);
        if (inode_fd < 0)
                return -errno;

        return connect_unix_inode(fd, inode_fd);
}

int socket_address_parse_unix(SocketAddress *ret_address, const char *s) {
        struct sockaddr_un un;
        int r;

        assert(ret_address);
        assert(s);

        if (!IN_SET(*s, '/', '@'))
                return -EPROTO;

        r = sockaddr_un_set_path(&un, s);
        if (r < 0)
                return r;

        *ret_address = (SocketAddress) {
                .sockaddr.un = un,
                .size = r,
        };

        return 0;
}

int vsock_parse_port(const char *s, unsigned *ret) {
        int r;

        assert(ret);

        if (!s)
                return -EINVAL;

        unsigned u;
        r = safe_atou(s, &u);
        if (r < 0)
                return r;

        /* Port 0 is apparently valid and not special in AF_VSOCK (unlike on IP). But VMADDR_PORT_ANY
         * (UINT32_MAX) is. Hence refuse that. */

        if (u == VMADDR_PORT_ANY)
                return -EINVAL;

        *ret = u;
        return 0;
}

int vsock_parse_cid(const char *s, unsigned *ret) {
        assert(ret);

        if (!s)
                return -EINVAL;

        /* Parsed an AF_VSOCK "CID". This is a 32bit entity, and the usual type is "unsigned". We recognize
         * the four special CIDs as strings, and otherwise parse the numeric CIDs. */

        if (streq(s, "hypervisor"))
                *ret = VMADDR_CID_HYPERVISOR;
        else if (streq(s, "local"))
                *ret = VMADDR_CID_LOCAL;
        else if (streq(s, "host"))
                *ret = VMADDR_CID_HOST;
        else if (STR_IN_SET(s, "any", "-1"))
                *ret = VMADDR_CID_ANY;
        else
                return safe_atou(s, ret);

        return 0;
}

int socket_address_parse_vsock(SocketAddress *ret_address, const char *s) {
        /* AF_VSOCK socket in vsock:cid:port notation */
        _cleanup_free_ char *n = NULL;
        const char *e, *cid_start;
        unsigned port, cid;
        int type, r;

        assert(ret_address);
        assert(s);

        if ((cid_start = startswith(s, "vsock:")))
                type = 0;
        else if ((cid_start = startswith(s, "vsock-dgram:")))
                type = SOCK_DGRAM;
        else if ((cid_start = startswith(s, "vsock-seqpacket:")))
                type = SOCK_SEQPACKET;
        else if ((cid_start = startswith(s, "vsock-stream:")))
                type = SOCK_STREAM;
        else
                return -EPROTO;

        e = strchr(cid_start, ':');
        if (!e)
                return -EINVAL;

        r = vsock_parse_port(e+1, &port);
        if (r < 0)
                return r;

        n = strndup(cid_start, e - cid_start);
        if (!n)
                return -ENOMEM;

        if (isempty(n))
                cid = VMADDR_CID_ANY;
        else {
                r = vsock_parse_cid(n, &cid);
                if (r < 0)
                        return r;
        }

        *ret_address = (SocketAddress) {
                .sockaddr.vm = {
                        .svm_family = AF_VSOCK,
                        .svm_cid = cid,
                        .svm_port = port,
                },
                .type = type,
                .size = sizeof(struct sockaddr_vm),
        };

        return 0;
}

void cmsg_close_all(struct msghdr *mh) {
        assert(mh);

        struct cmsghdr *cmsg;
        CMSG_FOREACH(cmsg, mh) {
                if (cmsg->cmsg_level != SOL_SOCKET)
                        continue;

                if (cmsg->cmsg_type == SCM_RIGHTS)
                        close_many(CMSG_TYPED_DATA(cmsg, int),
                                   (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int));
                else if (cmsg->cmsg_type == SCM_PIDFD) {
                        assert(cmsg->cmsg_len == CMSG_LEN(sizeof(int)));
                        safe_close(*CMSG_TYPED_DATA(cmsg, int));
                }
        }
}

int socket_xattr_supported(void) {
        int r;

        // FIXME: Drop this check once Linux 7.1 becomes our baseline

        static int cached = -1;
        if (cached >= 0)
                return cached;

        _cleanup_close_ int fd = socket(AF_UNIX, SOCK_DGRAM|SOCK_CLOEXEC, /* protocol= */ 0);
        if (fd < 0)
                return -errno;

        /* Old kernels return EPERM. But let's also check for more appropriate error codes, to be friendly to
         * seccomp policies */
        r = xsetxattr(fd, /* path= */ NULL, AT_EMPTY_PATH, "user.testxxx", "1");
        if (ERRNO_IS_NEG_NOT_SUPPORTED(r) || r == -EPERM)
                return (cached = false);
        if (r < 0)
                return log_debug_errno(r, "Failed to set test xattr on socket: %m");

        return (cached = true);
}
