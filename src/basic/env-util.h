/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include "forward.h"

#define ENVIRONMENT_ASSIGNMENTS_MAX 16384U

typedef enum ReplaceEnvFlags {
        REPLACE_ENV_USE_ENVIRONMENT = 1 << 0,
        REPLACE_ENV_ALLOW_BRACELESS = 1 << 1,
        REPLACE_ENV_ALLOW_EXTENDED  = 1 << 2,
} ReplaceEnvFlags;

int strv_env_get_merged(char **l, char ***ret);

int getenv_bool(const char *p);
int secure_getenv_bool(const char *p);

/* Like setenv, but calls unsetenv if value == NULL. */

/* Like putenv, but duplicates the memory like setenv. */

/* Parses and does sanity checks on an environment variable containing
 * PATH-like colon-separated absolute paths */

int setenvf(const char *name, bool overwrite, const char *valuef, ...) _printf_(3,4);
