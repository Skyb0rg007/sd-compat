/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include "forward.h"

size_t strpcpyf_full(char **dest, size_t size, bool *ret_truncated, const char *src, ...) _printf_(4, 5);
#define strpcpyf(dest, size, src, ...) \
        strpcpyf_full((dest), (size), NULL, (src), ##__VA_ARGS__)
size_t strpcpyl_full(char **dest, size_t size, bool *ret_truncated, const char *src, ...) _sentinel_;
#define strpcpyl(dest, size, src, ...) \
        strpcpyl_full((dest), (size), NULL, (src), ##__VA_ARGS__)
size_t strscpyl_full(char *dest, size_t size, bool *ret_truncated, const char *src, ...) _sentinel_;
#define strscpyl(dest, size, src, ...) \
        strscpyl_full(dest, size, NULL, src, ##__VA_ARGS__)
