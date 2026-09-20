/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <unistd.h>
#include <utmpx.h>

#include "sd-messages.h"

#include "fd-util.h"
#include "fileio.h"
#include "format-util.h"
#include "log.h"
#include "parse-util.h"
#include "path-util.h"
#include "string-util.h"
#include "strv.h"
#include "user-util.h"
#include "utf8.h"

#define DEFINE_STRERROR_ACCOUNT(type)                                   \
        const char* strerror_##type(                                    \
                        int errnum,                                     \
                        char *buf,                                      \
                        size_t buflen) {                                \
                                                                        \
                errnum = ABS(errnum);                                   \
                switch (errnum) {                                       \
                case ESRCH:                                             \
                        return "Unknown " STRINGIFY(type);              \
                case ENOEXEC:                                           \
                        return "Not a system " STRINGIFY(type);         \
                default:                                                \
                        return strerror_r(errnum, buf, buflen);         \
                }                                                       \
        }

bool uid_is_valid(uid_t uid) {

        /* Also see POSIX IEEE Std 1003.1-2008, 2016 Edition, 3.436. */

        /* Some libc APIs use UID_INVALID as special placeholder */
        if (uid == (uid_t) UINT32_C(0xFFFFFFFF))
                return false;

        /* A long time ago UIDs where 16 bit, hence explicitly avoid the 16-bit -1 too */
        if (uid == (uid_t) UINT32_C(0xFFFF))
                return false;

        return true;
}

int parse_uid(const char *s, uid_t *ret) {
        uint32_t uid = 0;
        int r;

        assert(s);

        assert_cc(sizeof(uid_t) == sizeof(uint32_t));

        /* We are very strict when parsing UIDs, and prohibit +/- as prefix, leading zero as prefix, and
         * whitespace. We do this, since this call is often used in a context where we parse things as UID
         * first, and if that doesn't work we fall back to NSS. Thus we really want to make sure that UIDs
         * are parsed as UIDs only if they really really look like UIDs. */
        r = safe_atou32_full(s, 10
                             | SAFE_ATO_REFUSE_PLUS_MINUS
                             | SAFE_ATO_REFUSE_LEADING_ZERO
                             | SAFE_ATO_REFUSE_LEADING_WHITESPACE, &uid);
        if (r < 0)
                return r;

        if (!uid_is_valid(uid))
                return -ENXIO; /* we return ENXIO instead of EINVAL
                                * here, to make it easy to distinguish
                                * invalid numeric uids from invalid
                                * strings. */

        if (ret)
                *ret = uid;

        return 0;
}

char* getusername_malloc(void) {
        const char *e;

        e = secure_getenv("USER");
        if (e)
                return strdup(e);

        return uid_to_name(getuid());
}

char* uid_to_name(uid_t uid) {
        char *ret;
        int r;

        /* Shortcut things to avoid NSS lookups */
        if (uid == 0)
                return strdup("root");
        if (uid == UID_NOBODY && synthesize_nobody())
                return strdup(NOBODY_USER_NAME);

        if (uid_is_valid(uid)) {
                _cleanup_free_ struct passwd *pw = NULL;

                r = getpwuid_malloc(uid, &pw);
                if (r >= 0)
                        return strdup(pw->pw_name);
        }

        if (asprintf(&ret, UID_FMT, uid) < 0)
                return NULL;

        return ret;
}

int get_home_dir(char **ret) {
        _cleanup_free_ struct passwd *p = NULL;
        const char *e;
        uid_t u;
        int r;

        assert(ret);

        /* Take the user specified one */
        e = secure_getenv("HOME");
        if (e && path_is_valid(e) && path_is_absolute(e))
                goto found;

        /* Hardcode home directory for root and nobody to avoid NSS */
        u = getuid();
        if (u == 0) {
                e = "/root";
                goto found;
        }
        if (u == UID_NOBODY && synthesize_nobody()) {
                e = "/";
                goto found;
        }

        /* Check the database... */
        r = getpwuid_malloc(u, &p);
        if (r < 0)
                return r;

        e = p->pw_dir;
        if (!path_is_valid(e) || !path_is_absolute(e))
                return -EINVAL;

 found:
        return path_simplify_alloc(e, ret);
}

int fully_set_uid_gid(uid_t uid, gid_t gid, const gid_t supplementary_gids[], size_t n_supplementary_gids) {
        int r;

        assert(supplementary_gids || n_supplementary_gids == 0);

        /* Sets all UIDs and all GIDs to the specified ones. Drops all auxiliary GIDs */

        r = maybe_setgroups(n_supplementary_gids, supplementary_gids);
        if (r < 0)
                return r;

        if (gid_is_valid(gid))
                if (setresgid(gid, gid, gid) < 0)
                        return -errno;

        if (uid_is_valid(uid))
                if (setresuid(uid, uid, uid) < 0)
                        return -errno;

        return 0;
}

bool valid_user_group_name(const char *u, ValidUserFlags flags) {
        const char *i;

        /* Checks if the specified name is a valid user/group name. There are two flavours of this call:
         * strict mode is the default which is POSIX plus some extra rules; and relaxed mode where we accept
         * pretty much everything except the really worst offending names.
         *
         * Whenever we synthesize users ourselves we should use the strict mode. But when we process users
         * created by other stuff, let's be more liberal. */

        if (isempty(u)) /* An empty user name is never valid */
                return false;

        if (parse_uid(u, NULL) >= 0) /* Something that parses as numeric UID string is valid exactly when the
                                      * flag for it is set */
                return FLAGS_SET(flags, VALID_USER_ALLOW_NUMERIC);

        if (FLAGS_SET(flags, VALID_USER_RELAX)) {

                /* In relaxed mode we just check very superficially. Apparently SSSD and other stuff is
                 * extremely liberal (way too liberal if you ask me, even inserting "@" in user names, which
                 * is bound to cause problems for example when used with an MTA), hence only filter the most
                 * obvious cases, or where things would result in an invalid entry if such a user name would
                 * show up in /etc/passwd (or equivalent getent output).
                 *
                 * Note that we stepped far out of POSIX territory here. It's not our fault though, but
                 * SSSD's, Samba's and everybody else who ignored POSIX on this. (I mean, I am happy to step
                 * outside of POSIX' bounds any day, but I must say in this case I probably wouldn't
                 * have...) */

                if (startswith(u, " ") || endswith(u, " ")) /* At least expect whitespace padding is removed
                                                             * at front and back (accept in the middle, since
                                                             * that's apparently a thing on Windows). Note
                                                             * that this also blocks usernames consisting of
                                                             * whitespace only. */
                        return false;

                if (!utf8_is_valid(u)) /* We want to synthesize JSON from this, hence insist on UTF-8 */
                        return false;

                if (string_has_cc(u, NULL)) /* CC characters are just dangerous (and \n in particular is the
                                             * record separator in /etc/passwd), so we can't allow that. */
                        return false;

                if (strpbrk(u, ":/")) /* Colons are the field separator in /etc/passwd, we can't allow
                                       * that. Slashes are special to file systems paths and user names
                                       * typically show up in the file system as home directories, hence
                                       * don't allow slashes. */
                        return false;

                if (in_charset(u, DIGITS)) /* Don't allow fully numeric strings, they might be confused with
                                            * UIDs (note that this test is more broad than the parse_uid()
                                            * test above, as it will cover more than the 32-bit range, and it
                                            * will detect 65535 (which is in invalid UID, even though in the
                                            * unsigned 32 bit range) */
                        return false;

                if (u[0] == '-' && in_charset(u + 1, DIGITS)) /* Don't allow negative fully numeric strings
                                                               * either. After all some people write 65535 as
                                                               * -1 (even though that's not even true on
                                                               * 32-bit uid_t anyway) */
                        return false;

                if (dot_or_dot_dot(u)) /* User names typically become home directory names, and these two are
                                        * special in that context, don't allow that. */
                        return false;

                /* Compare with strict result and warn if result doesn't match */
                if (FLAGS_SET(flags, VALID_USER_WARN) && !valid_user_group_name(u, 0))
                        log_struct(LOG_NOTICE,
                                   LOG_MESSAGE("Accepting user/group name '%s', which does not match strict user/group name rules.", u),
                                   LOG_ITEM("USER_GROUP_NAME=%s", u),
                                   LOG_MESSAGE_ID(SD_MESSAGE_UNSAFE_USER_NAME_STR));

                /* Note that we make no restrictions on the length in relaxed mode! */
        } else {
                long sz;
                size_t l;

                /* Also see POSIX IEEE Std 1003.1-2008, 2016 Edition, 3.437. We are a bit stricter here
                 * however. Specifically we deviate from POSIX rules:
                 *
                 * - We don't allow empty user names (see above)
                 * - We require that names fit into the appropriate utmp field
                 * - We don't allow any dots (this conflicts with chown syntax which permits dots as user/group name separator)
                 * - We don't allow dashes or digit as the first character
                 *
                 * Note that other systems are even more restrictive, and don't permit underscores or uppercase characters.
                 */

                if (!ascii_isalpha(u[0]) &&
                    u[0] != '_')
                        return false;

                for (i = u+1; *i; i++)
                        if (!ascii_isalpha(*i) &&
                            !ascii_isdigit(*i) &&
                            !IN_SET(*i, '_', '-'))
                                return false;

                l = i - u;

                sz = sysconf(_SC_LOGIN_NAME_MAX);
                assert_se(sz > 0);

                if (l > (size_t) sz) /* glibc: 256 */
                        return false;
                if (l > NAME_MAX) /* must fit in a filename: 255 */
                        return false;
                if (l > sizeof_field(struct utmpx, ut_user) - 1) /* must fit in utmp: 31 */
                        return false;
        }

        return true;
}

int maybe_setgroups(size_t size, const gid_t *list) {
        int r;

        /* Check if setgroups is allowed before we try to drop all the auxiliary groups */
        if (size == 0) { /* Dropping all aux groups? */

                /* The kernel refuses setgroups() if there are no GID mappings in the current
                 * user namespace, so check that beforehand and don't try to setgroups() if
                 * there are no GID mappings. */
                _cleanup_fclose_ FILE *f = fopen("/proc/self/gid_map", "re");
                if (!f && errno != ENOENT)
                        return -errno;
                if (f) {
                        r = safe_fgetc(f, /* ret= */ NULL);
                        if (r < 0)
                                return r;
                        if (r == 0) {
                                log_debug("Skipping setgroups(), /proc/self/gid_map is empty");
                                return 0;
                        }
                }

                _cleanup_free_ char *setgroups_content = NULL;
                r = read_one_line_file("/proc/self/setgroups", &setgroups_content);
                if (r < 0 && r != -ENOENT)
                        return r;
                if (r > 0 && streq(setgroups_content, "deny")) {
                        log_debug("Skipping setgroups(), /proc/self/setgroups is set to 'deny'");
                        return 0;
                }
        }

        return RET_NERRNO(setgroups(size, list));
}

bool synthesize_nobody(void) {
        /* Returns true when we shall synthesize the "nobody" user (which we do by default). This can be turned off by
         * touching /etc/systemd/dont-synthesize-nobody in order to provide upgrade compatibility with legacy systems
         * that used the "nobody" user name and group name for other UIDs/GIDs than 65534.
         *
         * Note that we do not employ any kind of synchronization on the following caching variable. If the variable is
         * accessed in multi-threaded programs in the worst case it might happen that we initialize twice, but that
         * shouldn't matter as each initialization should come to the same result. */
        static int cache = -1;

        if (cache < 0)
                cache = access("/etc/systemd/dont-synthesize-nobody", F_OK) < 0;

        return cache;
}

#if ENABLE_GSHADOW
int putsgent_sane(const struct sgrp *sg, FILE *stream) {
        assert(sg);
        assert(stream);

        errno = 0;
        if (putsgent(sg, stream) != 0)
                return errno_or_else(EIO);

        return 0;
}
#endif

int fgetpwent_sane(FILE *stream, struct passwd **pw) {
        assert(stream);
        assert(pw);

        errno = 0;
        struct passwd *p = fgetpwent(stream);
        if (!p && !IN_SET(errno, 0, ENOENT))
                return -errno;

        *pw = p;
        return !!p;
}

#if ENABLE_GSHADOW
int fgetsgent_sane(FILE *stream, struct sgrp **sg) {
        assert(stream);
        assert(sg);

        errno = 0;
        struct sgrp *s = fgetsgent(stream);
        if (!s && !IN_SET(errno, 0, ENOENT))
                return -errno;

        *sg = s;
        return !!s;
}
#endif

static int copy_struct_passwd(const struct passwd *pw, struct passwd **ret) {
        assert(pw);
        assert(ret);

        size_t need_bytes = sizeof(struct passwd)
                + strlen_ptr(pw->pw_name) + 1
                + strlen_ptr(pw->pw_passwd) + 1
                + strlen_ptr(pw->pw_gecos) + 1
                + strlen_ptr(pw->pw_dir) + 1
                + strlen_ptr(pw->pw_shell) + 1;

        char *buf = malloc(need_bytes);
        if (!buf)
                return -ENOMEM;

        struct passwd *newpw = (struct passwd *) buf;

        /* The layout in our buffer:
         * struct passwd, and then individual strings. */
        char *p = buf + sizeof(struct passwd);

        newpw->pw_name = p;
        p = stpcpy(p, strempty(pw->pw_name)) + 1;

        newpw->pw_passwd = p;
        p = stpcpy(p, strempty(pw->pw_passwd)) + 1;

        newpw->pw_uid = pw->pw_uid;
        newpw->pw_gid = pw->pw_gid;

        newpw->pw_gecos = p;
        p = stpcpy(p, strempty(pw->pw_gecos)) + 1;

        newpw->pw_dir = p;
        p = stpcpy(p, strempty(pw->pw_dir)) + 1;

        newpw->pw_shell = p;
        p = stpcpy(p, strempty(pw->pw_shell)) + 1;

        *ret = newpw;
        return 0;
}

/* Iterate the given list of passwd-format files looking for an entry matching the predicate (by
 * name if 'name' is non-NULL and by 'uid' if valid). Returns -ESRCH if no entry is found. */
int lookup_pwent_in_files(
                char * const *files,
                const char *name,
                uid_t uid,
                struct passwd **ret) {

        int r;

        assert(files);
        assert(name || uid_is_valid(uid));

        STRV_FOREACH(fname, files) {
                _cleanup_fclose_ FILE *f = NULL;
                struct passwd *pw;

                r = fopen_unlocked(*fname, "re", &f);
                if (r == -ENOENT)
                        continue;
                if (r < 0)
                        return r;

                while ((r = fgetpwent_sane(f, &pw)) > 0) {
                        if (name && !streq_ptr(pw->pw_name, name))
                                continue;
                        if (uid_is_valid(uid) && pw->pw_uid != uid)
                                continue;
                        if (ret)
                                return copy_struct_passwd(pw, ret);
                        return 0;
                }
                if (r < 0)
                        return r;
        }

        return -ESRCH;
}

#if !BUILD_STATIC

static size_t getpw_buffer_size(void) {
        long bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
        return bufsize <= 0 ? 4096U : (size_t) bufsize;
}

static bool errno_is_user_doesnt_exist(int error) {
        /* See getpwnam(3) and getgrnam(3): those codes and others can be returned if the user or group are
         * not found. */
        return IN_SET(abs(error), ENOENT, ESRCH, EBADF, EPERM);
}

#endif

int getpwuid_malloc(uid_t uid, struct passwd **ret) {
        if (!uid_is_valid(uid))
                return -EINVAL;

#if BUILD_STATIC
        return lookup_pwent_in_files(PASSWD_FILES, /* name= */ NULL, uid, ret);
#else
        size_t bufsize = getpw_buffer_size();
        int r;

        for (;;) {
                _cleanup_free_ void *buf = NULL;

                /* Silence static analyzers */
                assert(bufsize <= SIZE_MAX - ALIGN(sizeof(struct passwd)));
                buf = malloc0(ALIGN(sizeof(struct passwd)) + bufsize);
                if (!buf)
                        return -ENOMEM;

                struct passwd *pw = NULL;
                r = getpwuid_r(uid, buf, (char*) buf + ALIGN(sizeof(struct passwd)), bufsize, &pw);
                if (r == 0) {
                        if (pw) {
                                if (ret)
                                        *ret = TAKE_PTR(buf);
                                return 0;
                        }

                        return -ESRCH;
                }

                assert(r > 0);

                if (errno_is_user_doesnt_exist(r))
                        return -ESRCH;
                if (r != ERANGE)
                        return -r;

                if (bufsize > SIZE_MAX/2 - ALIGN(sizeof(struct passwd)))
                        return -ENOMEM;
                bufsize *= 2;
        }
#endif
}

/* See lookup_pwent_in_files() for the analogous passwd-file version. */
int sysconf_ngroups_max(void) {
        /* Query sysconf _SC_NGROUPS_MAX. Returns an int because the expected value is 64k
         * and later on this is used as an int with various glibc consumers. */

        errno = 0;
        long ngroups_max = sysconf(_SC_NGROUPS_MAX);
        if (ngroups_max <= 0)
                return errno_or_else(EOPNOTSUPP);
        if (ngroups_max > INT_MAX)
                return -ERANGE;
        return ngroups_max;
}

#if !BUILD_STATIC

#endif

