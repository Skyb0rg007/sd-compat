/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <linux/magic.h>
#include <linux/sockios.h>
#include <linux/nsfs.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <unistd.h>

#include "capability-util.h"
#include "dlfcn-util.h"
#include "fd-util.h"
#include "log.h"
#include "namespace-util.h"
#include "pidfd-util.h"
#include "pidref.h"
#include "process-util.h"
#include "stat-util.h"
#include "user-util.h"

const struct namespace_info namespace_info[_NAMESPACE_TYPE_MAX + 1] = {
        [NAMESPACE_CGROUP] =  { "cgroup", "ns/cgroup", CLONE_NEWCGROUP, PIDFD_GET_CGROUP_NAMESPACE, PROC_CGROUP_INIT_INO },
        [NAMESPACE_IPC]    =  { "ipc",    "ns/ipc",    CLONE_NEWIPC,    PIDFD_GET_IPC_NAMESPACE,    PROC_IPC_INIT_INO    },
        [NAMESPACE_NET]    =  { "net",    "ns/net",    CLONE_NEWNET,    PIDFD_GET_NET_NAMESPACE,    0                    },
        /* So, the mount namespace flag is called CLONE_NEWNS for historical
         * reasons. Let's expose it here under a more explanatory name: "mnt".
         * This is in-line with how the kernel exposes namespaces in /proc/$PID/ns. */
        [NAMESPACE_MOUNT]  =  { "mnt",    "ns/mnt",    CLONE_NEWNS,     PIDFD_GET_MNT_NAMESPACE,    0                    },
        [NAMESPACE_PID]    =  { "pid",    "ns/pid",    CLONE_NEWPID,    PIDFD_GET_PID_NAMESPACE,    PROC_PID_INIT_INO    },
        [NAMESPACE_USER]   =  { "user",   "ns/user",   CLONE_NEWUSER,   PIDFD_GET_USER_NAMESPACE,   PROC_USER_INIT_INO   },
        [NAMESPACE_UTS]    =  { "uts",    "ns/uts",    CLONE_NEWUTS,    PIDFD_GET_UTS_NAMESPACE,    PROC_UTS_INIT_INO    },
        [NAMESPACE_TIME]   =  { "time",   "ns/time",   CLONE_NEWTIME,   PIDFD_GET_TIME_NAMESPACE,   PROC_TIME_INIT_INO   },
        {}, /* Allow callers to iterate over the array without using _NAMESPACE_TYPE_MAX. */
};

#define pid_namespace_path(pid, type) procfs_file_alloca(pid, namespace_info[type].proc_path)

NamespaceType clone_flag_to_namespace_type(unsigned long clone_flag) {
        for (NamespaceType t = 0; t < _NAMESPACE_TYPE_MAX; t++)
                if (((namespace_info[t].clone_flag ^ clone_flag) & (CLONE_NEWCGROUP|CLONE_NEWIPC|CLONE_NEWNET|CLONE_NEWNS|CLONE_NEWPID|CLONE_NEWUSER|CLONE_NEWUTS|CLONE_NEWTIME)) == 0)
                        return t;

        return _NAMESPACE_TYPE_INVALID;
}

static int pidref_namespace_open_by_type_internal(const PidRef *pidref, NamespaceType type, bool *need_verify) {
        int r;

        assert(pidref_is_set(pidref));
        assert(type >= 0 && type < _NAMESPACE_TYPE_MAX);

        if (pidref_is_remote(pidref))
                return -EREMOTE;

        if (pidref->fd >= 0) {
                r = pidfd_get_namespace(pidref->fd, namespace_info[type].pidfd_get_ns_ioctl_cmd);
                if (r == -ENOPKG)
                        return log_debug_errno(
                                        r,
                                        "Cannot open %s namespace for PID "PID_FMT" as the namespace type is not supported by the kernel",
                                        namespace_info[type].proc_name, pidref->pid);
                if (!ERRNO_IS_NEG_NOT_SUPPORTED(r))
                        return r;
        }

        if (need_verify) /* The caller shall call pidref_verify() later */
                *need_verify = true;

        _cleanup_close_ int nsfd = -EBADF;
        const char *p;

        p = pid_namespace_path(pidref->pid, type);
        nsfd = RET_NERRNO(open(p, O_RDONLY|O_NOCTTY|O_CLOEXEC));
        if (nsfd == -ENOENT) {
                r = proc_mounted();
                if (r == 0)
                        /* /proc/ is not available or not set up properly, we're most likely in some chroot environment. */
                        return log_debug_errno(
                                        SYNTHETIC_ERRNO(ENOSYS),
                                        "Cannot open %s namespace for PID "PID_FMT" as /proc is not mounted",
                                        namespace_info[type].proc_name, pidref->pid);
                if (r > 0)
                        /* If /proc/ is definitely around then this means the namespace type is not supported */
                        return log_debug_errno(
                                        SYNTHETIC_ERRNO(ENOPKG),
                                        "Cannot open %s namespace for PID "PID_FMT" via /proc as the namespace type is not supported by the kernel",
                                        namespace_info[type].proc_name, pidref->pid);

                /* can't determine? then propagate original error */
        }
        if (nsfd < 0)
                return nsfd;

        if (!need_verify) { /* Otherwise we verify on our own */
                r = pidref_verify(pidref);
                if (r < 0)
                        return r;
        }

        return TAKE_FD(nsfd);
}

int pidref_namespace_open_by_type(const PidRef *pidref, NamespaceType type) {
        return pidref_namespace_open_by_type_internal(pidref, type, NULL);
}

int namespace_open_by_type(NamespaceType type) {
        _cleanup_(pidref_done) PidRef self = PIDREF_NULL;
        int r;

        assert(type >= 0 && type < _NAMESPACE_TYPE_MAX);

        r = pidref_set_self(&self);
        if (r < 0)
                return r;

        return pidref_namespace_open_by_type(&self, type);
}

int pidref_namespace_open(
                const PidRef *pidref,
                int *ret_pidns_fd,
                int *ret_mntns_fd,
                int *ret_netns_fd,
                int *ret_userns_fd,
                int *ret_root_fd) {

        _cleanup_close_ int pidns_fd = -EBADF, mntns_fd = -EBADF, netns_fd = -EBADF,
                userns_fd = -EBADF, root_fd = -EBADF;
        bool need_verify = false;
        int r;

        assert(pidref_is_set(pidref));

        if (pidref_is_remote(pidref))
                return -EREMOTE;

        if (ret_pidns_fd) {
                pidns_fd = pidref_namespace_open_by_type_internal(pidref, NAMESPACE_PID, &need_verify);
                if (pidns_fd < 0)
                        return pidns_fd;
        }

        if (ret_mntns_fd) {
                mntns_fd = pidref_namespace_open_by_type_internal(pidref, NAMESPACE_MOUNT, &need_verify);
                if (mntns_fd < 0)
                        return mntns_fd;
        }

        if (ret_netns_fd) {
                netns_fd = pidref_namespace_open_by_type_internal(pidref, NAMESPACE_NET, &need_verify);
                if (netns_fd < 0)
                        return netns_fd;
        }

        if (ret_userns_fd) {
                userns_fd = pidref_namespace_open_by_type_internal(pidref, NAMESPACE_USER, &need_verify);
                if (userns_fd < 0 && userns_fd != -ENOPKG)
                        return userns_fd;
        }

        if (ret_root_fd) {
                const char *root;

                root = procfs_file_alloca(pidref->pid, "root");
                root_fd = RET_NERRNO(open(root, O_CLOEXEC|O_DIRECTORY));
                if (root_fd == -ENOENT && proc_mounted() == 0)
                        return -ENOSYS;
                if (root_fd < 0)
                        return root_fd;

                need_verify = true;
        }

        if (need_verify) {
                r = pidref_verify(pidref);
                if (r < 0)
                        return r;
        }

        if (ret_pidns_fd)
                *ret_pidns_fd = TAKE_FD(pidns_fd);

        if (ret_mntns_fd)
                *ret_mntns_fd = TAKE_FD(mntns_fd);

        if (ret_netns_fd)
                *ret_netns_fd = TAKE_FD(netns_fd);

        if (ret_userns_fd)
                *ret_userns_fd = TAKE_FD(userns_fd);

        if (ret_root_fd)
                *ret_root_fd = TAKE_FD(root_fd);

        return 0;
}

int namespace_open(
                pid_t pid,
                int *ret_pidns_fd,
                int *ret_mntns_fd,
                int *ret_netns_fd,
                int *ret_userns_fd,
                int *ret_root_fd) {

        _cleanup_(pidref_done) PidRef pidref = PIDREF_NULL;
        int r;

        r = pidref_set_pid(&pidref, pid);
        if (r < 0)
                return r;

        return pidref_namespace_open(&pidref, ret_pidns_fd, ret_mntns_fd, ret_netns_fd, ret_userns_fd, ret_root_fd);
}

int namespace_enter(int pidns_fd, int mntns_fd, int netns_fd, int userns_fd, int root_fd) {
        int r;

        /* Block dlopen() now, to avoid us inadvertently loading shared library from another namespace */
        block_dlopen();

         /* Join namespaces, but only if we're not part of them already. This is important if we don't
          * necessarily own the namespace in question, as kernel would unconditionally return EPERM otherwise. */

        if (pidns_fd >= 0) {
                r = is_our_namespace(pidns_fd, NAMESPACE_PID);
                if (r < 0)
                        return r;
                if (r > 0)
                        pidns_fd = -EBADF;
        }

        if (mntns_fd >= 0) {
                r = is_our_namespace(mntns_fd, NAMESPACE_MOUNT);
                if (r < 0)
                        return r;
                if (r > 0)
                        mntns_fd = -EBADF;
        }

        if (netns_fd >= 0) {
                r = is_our_namespace(netns_fd, NAMESPACE_NET);
                if (r < 0)
                        return r;
                if (r > 0)
                        netns_fd = -EBADF;
        }

        if (userns_fd >= 0) {
                /* Can't setns to your own userns, since then you could escalate from non-root to root in
                 * your own namespace, so check if namespaces are equal before attempting to enter. */

                r = is_our_namespace(userns_fd, NAMESPACE_USER);
                if (r < 0)
                        return r;
                if (r > 0)
                        userns_fd = -EBADF;
        }

        r = have_effective_cap(CAP_SYS_ADMIN);
        if (r < 0)
                return r;

        bool have_cap_sys_admin = r > 0;

        if (!have_cap_sys_admin) {
                /* If we don't have CAP_SYS_ADMIN in our own user namespace, our best bet is to enter the
                 * user namespace first (if we got one) to get CAP_SYS_ADMIN within the child user namespace,
                 * and then hope the other namespaces are owned by the child user namespace. If they aren't,
                 * we'll just get an EPERM later on when trying to setns() to them. */

                if (userns_fd < 0)
                        return log_debug_errno(
                                        SYNTHETIC_ERRNO(EPERM),
                                        "Need CAP_SYS_ADMIN or a child user namespace to enter namespaces.");

                if (setns(userns_fd, CLONE_NEWUSER) < 0)
                        return -errno;
        }

        if (pidns_fd >= 0)
                if (setns(pidns_fd, CLONE_NEWPID) < 0)
                        return -errno;

        if (mntns_fd >= 0)
                if (setns(mntns_fd, CLONE_NEWNS) < 0)
                        return -errno;

        if (netns_fd >= 0)
                if (setns(netns_fd, CLONE_NEWNET) < 0)
                        return -errno;

        if (userns_fd >= 0 && have_cap_sys_admin)
                if (setns(userns_fd, CLONE_NEWUSER) < 0)
                        return -errno;

        if (root_fd >= 0) {
                if (fchdir(root_fd) < 0)
                        return -errno;

                if (chroot(".") < 0)
                        return -errno;
        }

        if (userns_fd >= 0) {
                /* Try to become root in the user namespace but don't error out if we can't, since it's not
                 * uncommon to have user namespaces without a root user in them. */
                r = reset_uid_gid();
                if (r < 0)
                        log_debug_errno(r, "Unable to drop auxiliary groups or reset UID/GID, ignoring: %m");
        }

        return 0;
}

int fd_is_namespace(int fd, NamespaceType type) {
        int r;

        /* Checks whether the specified file descriptor refers to a namespace (of type if type != _NAMESPACE_INVALID). */

        assert(fd >= 0);
        assert(type < _NAMESPACE_TYPE_MAX);

        r = fd_is_fs_type(fd, NSFS_MAGIC);
        if (r <= 0)
                return r;

        if (type < 0)
                return true;

        int clone_flag = ioctl(fd, NS_GET_NSTYPE);
        if (clone_flag < 0)
                return -errno;

        NamespaceType found_type = clone_flag_to_namespace_type(clone_flag);
        if (found_type < 0)
                return -EBADF; /* Uh? Unknown namespace type? */

        return found_type == type;
}

int is_our_namespace(int fd, NamespaceType type) {
        int r;

        assert(fd >= 0);
        assert(type >= 0 && type < _NAMESPACE_TYPE_MAX);

        r = fd_is_namespace(fd, type);
        if (r < 0)
                return r;
        if (r == 0) /* Not a namespace or not of the right type? */
                return -EUCLEAN;

        _cleanup_close_ int our_ns = namespace_open_by_type(type);
        if (our_ns < 0)
                return our_ns;

        return fd_inode_same(fd, our_ns);
}

int are_our_namespaces(int pidns_fd, int mntns_fd, int netns_fd, int userns_fd, int root_fd) {
        int r;

        if (pidns_fd >= 0) {
                r = is_our_namespace(pidns_fd, NAMESPACE_PID);
                if (r <= 0)
                        return r;
        }

        if (mntns_fd >= 0) {
                r = is_our_namespace(mntns_fd, NAMESPACE_MOUNT);
                if (r <= 0)
                        return r;
        }

        if (netns_fd >= 0) {
                r = is_our_namespace(netns_fd, NAMESPACE_NET);
                if (r <= 0)
                        return r;
        }

        if (userns_fd >= 0) {
                r = is_our_namespace(userns_fd, NAMESPACE_USER);
                if (r <= 0)
                        return r;
        }

        if (root_fd >= 0) {
                r = dir_fd_is_root(root_fd);
                if (r <= 0)
                        return r;
        }

        return true;
}

int network_namespace_is_init(int socket_fd) {
        struct stat a, b;
        int r;

        /* This works only when privileged. */

        r = RET_NERRNO(stat(pid_namespace_path(1, NAMESPACE_NET), &a));
        if (r == -ENOENT) {
                /* If the /proc/ns/<type> API is not around in /proc/ then ns is off in the kernel and we are in the init ns */
                r = proc_mounted();
                if (r < 0)
                        return -ENOENT; /* If we can't determine if /proc/ is mounted propagate original error */

                return r ? true : -ENOSYS;
        }
        if (r < 0)
                return r;

        if (socket_fd >= 0) {
                _cleanup_close_ int netns = ioctl(socket_fd, SIOCGSKNS);
                if (netns < 0)
                        return -errno;

                if (fstat(netns, &b) < 0)
                        return -errno;
        } else {
                if (stat(pid_namespace_path(0, NAMESPACE_NET), &b) < 0)
                        return -errno;
        }

        return stat_inode_same(&a, &b);
}

int namespace_is_init(NamespaceType type) {
        int r;

        assert(type >= 0);
        assert(type < _NAMESPACE_TYPE_MAX);

        if (type == NAMESPACE_NET)
                return network_namespace_is_init(/* socket_fd= */ -EBADF);

        if (namespace_info[type].root_inode == 0)
                return -EBADR; /* Cannot answer this question */

        const char *p = pid_namespace_path(0, type);

        struct stat st;
        r = RET_NERRNO(stat(p, &st));
        if (r == -ENOENT) {
                /* If the /proc/ns/<type> API is not around in /proc/ then ns is off in the kernel and we are in the init ns */
                r = proc_mounted();
                if (r < 0)
                        return -ENOENT; /* If we can't determine if /proc/ is mounted propagate original error */

                return r ? true : -ENOSYS;
        }
        if (r < 0)
                return r;

        return st.st_ino == namespace_info[type].root_inode;
}

