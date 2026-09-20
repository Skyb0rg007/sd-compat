/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <sys/stat.h>

#include "chase.h"
#include "devnum-util.h"
#include "stdio-util.h"
#include "string-util.h"

int device_path_make_major_minor(mode_t mode, dev_t devnum, char **ret) {
        const char *t;

        /* Generates the /dev/{char|block}/MAJOR:MINOR path for a dev_t */

        if (S_ISCHR(mode))
                t = "char";
        else if (S_ISBLK(mode))
                t = "block";
        else
                return -ENODEV;

        if (asprintf(ret, "/dev/%s/" DEVNUM_FORMAT_STR, t, DEVNUM_FORMAT_VAL(devnum)) < 0)
                return -ENOMEM;

        return 0;
}

int device_path_make_inaccessible(mode_t mode, char **ret) {
        const char *s;

        assert(ret);

        if (S_ISCHR(mode))
                s = "/run/systemd/inaccessible/chr";
        else if (S_ISBLK(mode))
                s = "/run/systemd/inaccessible/blk";
        else
                return -ENODEV;

        return strdup_to(ret, s);
}

int device_path_make_canonical(mode_t mode, dev_t devnum, char **ret) {
        _cleanup_free_ char *p = NULL;
        int r;

        /* Finds the canonical path for a device, i.e. resolves the /dev/{char|block}/MAJOR:MINOR path to the end. */

        assert(ret);

        if (devnum_is_zero(devnum))
                /* A special hack to make sure our 'inaccessible' device nodes work. They won't have symlinks in
                 * /dev/block/ and /dev/char/, hence we handle them specially here. */
                return device_path_make_inaccessible(mode, ret);

        r = device_path_make_major_minor(mode, devnum, &p);
        if (r < 0)
                return r;

        return chase(p, NULL, 0, ret, NULL);
}

