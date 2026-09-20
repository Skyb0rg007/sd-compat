/* SPDX-License-Identifier: LGPL-2.1-or-later */


#include "capsule-util.h"
#include "fd-util.h"
#include "login-util.h"
#include "path-util.h"
#include "process-util.h"
#include "special.h"
#include "unit-name.h"
#include "user-util.h"

int cg_pid_get_path(pid_t pid, char **ret_path) {
        _cleanup_fclose_ FILE *f = NULL;
        const char *fs;
        int r;

        assert(pid >= 0);
        assert(ret_path);

        fs = procfs_file_alloca(pid, "cgroup");
        r = fopen_unlocked(fs, "re", &f);
        if (r == -ENOENT)
                return -ESRCH;
        if (r < 0)
                return r;

        for (;;) {
                _cleanup_free_ char *line = NULL;
                char *e;

                r = read_line(f, LONG_LINE_MAX, &line);
                if (r < 0)
                        return r;
                if (r == 0)
                        return -ENODATA;

                e = startswith(line, "0:");
                if (!e)
                        continue;

                e = strchr(e, ':');
                if (!e)
                        continue;

                _cleanup_free_ char *path = strdup(e + 1);
                if (!path)
                        return -ENOMEM;

                /* Refuse cgroup paths from outside our cgroup namespace */
                if (startswith(path, "/../"))
                        return -EUNATCH;

                /* Truncate suffix indicating the process is a zombie */
                e = endswith(path, " (deleted)");
                if (e)
                        *e = 0;

                *ret_path = TAKE_PTR(path);
                return 0;
        }
}

int cg_get_root_path(char **ret_path) {
        char *p, *e;
        int r;

        assert(ret_path);

        r = cg_pid_get_path(1, &p);
        if (r < 0)
                return r;

        e = endswith(p, "/" SPECIAL_INIT_SCOPE);
        if (e)
                *e = 0;

        *ret_path = p;
        return 0;
}

int cg_shift_path(const char *cgroup, const char *root, const char **ret_shifted) {
        int r;

        assert(cgroup);
        assert(ret_shifted);

        _cleanup_free_ char *rt = NULL;
        if (!root) {
                /* If the root was specified let's use that, otherwise
                 * let's determine it from PID 1 */

                r = cg_get_root_path(&rt);
                if (r < 0)
                        return r;

                root = rt;
        }

        *ret_shifted = path_startswith_full(cgroup, root, PATH_STARTSWITH_RETURN_LEADING_SLASH|PATH_STARTSWITH_REFUSE_DOT_DOT) ?: cgroup;
        return 0;
}

int cg_pid_get_path_shifted(pid_t pid, const char *root, char **ret_cgroup) {
        _cleanup_free_ char *raw = NULL;
        const char *c;
        int r;

        assert(pid >= 0);
        assert(ret_cgroup);

        r = cg_pid_get_path(pid, &raw);
        if (r < 0)
                return r;

        r = cg_shift_path(raw, root, &c);
        if (r < 0)
                return r;

        if (c == raw) {
                *ret_cgroup = TAKE_PTR(raw);
                return 0;
        }

        return strdup_to(ret_cgroup, c);
}

int cg_path_decode_unit(const char *cgroup, char **ret_unit) {
        assert(cgroup);

        size_t n = strcspn(cgroup, "/");
        if (n < 3)
                return -ENXIO;

        char *c = strndupa_safe(cgroup, n);
        c = cg_unescape(c);

        if (!unit_name_is_valid(c, UNIT_NAME_PLAIN|UNIT_NAME_INSTANCE))
                return -ENXIO;

        if (ret_unit)
                return strdup_to(ret_unit, c);

        return 0;
}

static bool valid_slice_name(const char *p, size_t n) {
        assert(p || n == 0);

        if (n < STRLEN("x.slice"))
                return false;

        char *c = strndupa_safe(p, n);
        if (!endswith(c, ".slice"))
                return false;

        return unit_name_is_valid(cg_unescape(c), UNIT_NAME_PLAIN);
}

static const char* skip_slices(const char *p) {
        assert(p);

        /* Skips over all slice assignments */

        for (;;) {
                size_t n;

                p += strspn(p, "/");

                n = strcspn(p, "/");
                if (!valid_slice_name(p, n))
                        return p;

                p += n;
        }
}

int cg_path_get_unit_full(const char *path, char **ret_unit, char **ret_subgroup) {
        int r;

        assert(path);

        const char *e = skip_slices(path);

        _cleanup_free_ char *unit = NULL;
        r = cg_path_decode_unit(e, &unit);
        if (r < 0)
                return r;

        /* We skipped over the slices, don't accept any now */
        if (endswith(unit, ".slice"))
                return -ENXIO;

        if (ret_subgroup) {
                _cleanup_free_ char *subgroup = NULL;
                e += strcspn(e, "/");
                e += strspn(e, "/");

                if (isempty(e))
                        subgroup = NULL;
                else {
                        subgroup = strdup(e);
                        if (!subgroup)
                                return -ENOMEM;
                }

                path_simplify(subgroup);

                *ret_subgroup = TAKE_PTR(subgroup);
        }

        if (ret_unit)
                *ret_unit = TAKE_PTR(unit);

        return 0;
}

static const char* skip_session(const char *p) {
        size_t n;

        /* Skip session-*.scope, but require it to be there. */

        if (isempty(p))
                return NULL;

        p += strspn(p, "/");

        n = strcspn(p, "/");
        if (n < STRLEN("session-x.scope"))
                return NULL;

        const char *s = startswith(p, "session-");
        if (!s)
                return NULL;

        /* Note that session scopes never need unescaping, since they cannot conflict with the kernel's
         * own names, hence we don't need to call cg_unescape() here. */
        char *f = strndupa_safe(s, p + n - s),
             *e = endswith(f, ".scope");
        if (!e)
                return NULL;
        *e = '\0';

        if (!session_id_valid(f))
                return NULL;

        return skip_leading_slash(p + n);
}

static const char* skip_user_manager(const char *p) {
        size_t n;

        /* Skip user@*.service or capsule@*.service, but require either of them to be there. */

        if (isempty(p))
                return NULL;

        p += strspn(p, "/");

        n = strcspn(p, "/");
        if (n < CONST_MIN(STRLEN("user@x.service"), STRLEN("capsule@x.service")))
                return NULL;

        /* Any possible errors from functions called below are converted to NULL return, so our callers won't
         * resolve user/capsule name. */
        _cleanup_free_ char *unit_name = strndup(p, n);
        if (!unit_name)
                return NULL;

        _cleanup_free_ char *i = NULL;
        UnitNameFlags type = unit_name_to_instance(unit_name, &i);

        if (type != UNIT_NAME_INSTANCE)
                return NULL;

        /* Note that user manager services never need unescaping, since they cannot conflict with the
         * kernel's own names, hence we don't need to call cg_unescape() here.  Prudently check validity of
         * instance names, they should be always valid as we validate them upon unit start. */
        if (!(startswith(unit_name, "user@") && parse_uid(i, NULL) >= 0) &&
            !(startswith(unit_name, "capsule@") && capsule_name_is_valid(i) > 0))
                return NULL;

        return skip_leading_slash(p + n);
}

static const char* skip_user_prefix(const char *path) {
        const char *e, *t;

        assert(path);

        /* Skip slices, if there are any */
        e = skip_slices(path);

        /* Skip the user manager, if it's in the path now... */
        t = skip_user_manager(e);
        if (t)
                return t;

        /* Alternatively skip the user session if it is in the path... */
        return skip_session(e);
}

int cg_path_get_user_unit_full(const char *path, char **ret_unit, char **ret_subgroup) {
        const char *t;

        assert(path);

        t = skip_user_prefix(path);
        if (!t)
                return -ENXIO;

        /* And from here on it looks pretty much the same as for a system unit, hence let's use the same
         * parser. */
        return cg_path_get_unit_full(t, ret_unit, ret_subgroup);
}

int cg_path_get_session(const char *path, char **ret_session) {
        _cleanup_free_ char *unit = NULL;
        char *start, *end;
        int r;

        assert(path);

        r = cg_path_get_unit(path, &unit);
        if (r < 0)
                return r;

        start = startswith(unit, "session-");
        if (!start)
                return -ENXIO;
        end = endswith(start, ".scope");
        if (!end)
                return -ENXIO;

        *end = 0;
        if (!session_id_valid(start))
                return -ENXIO;

        if (!ret_session)
                return 0;

        return strdup_to(ret_session, start);
}

int cg_path_get_owner_uid(const char *path, uid_t *ret_uid) {
        _cleanup_free_ char *slice = NULL;
        char *start, *end;
        int r;

        assert(path);

        r = cg_path_get_slice(path, &slice);
        if (r < 0)
                return r;

        start = startswith(slice, "user-");
        if (!start)
                return -ENXIO;

        end = endswith(start, ".slice");
        if (!end)
                return -ENXIO;

        *end = 0;
        if (parse_uid(start, ret_uid) < 0)
                return -ENXIO;

        return 0;
}

int cg_pid_get_owner_uid(pid_t pid, uid_t *ret_uid) {
        _cleanup_free_ char *cgroup = NULL;
        int r;

        r = cg_pid_get_path_shifted(pid, NULL, &cgroup);
        if (r < 0)
                return r;

        return cg_path_get_owner_uid(cgroup, ret_uid);
}

int cg_path_get_slice(const char *p, char **ret_slice) {
        const char *e = NULL;

        assert(p);

        /* Finds the right-most slice unit from the beginning, but stops before we come to
         * the first non-slice unit. */

        for (;;) {
                const char *s;
                int n;

                n = path_find_first_component(&p, /* accept_dot_dot= */ false, &s);
                if (n < 0)
                        return n;
                if (!valid_slice_name(s, n))
                        break;

                e = s;
        }

        if (e)
                return cg_path_decode_unit(e, ret_slice);

        if (ret_slice)
                return strdup_to(ret_slice, SPECIAL_ROOT_SLICE);

        return 0;
}

int cg_path_get_user_slice(const char *p, char **ret_slice) {
        const char *t;
        assert(p);

        t = skip_user_prefix(p);
        if (!t)
                return -ENXIO;

        /* And now it looks pretty much the same as for a system slice, so let's just use the same parser
         * from here on. */
        return cg_path_get_slice(t, ret_slice);
}

char* cg_unescape(const char *p) {
        assert(p);

        /* The return value of this function (unlike cg_escape())
         * doesn't need free()! */

        if (p[0] == '_')
                return (char*) p+1;

        return (char*) p;
}

