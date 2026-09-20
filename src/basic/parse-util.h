/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include "forward.h"

typedef unsigned long loadavg_t;

int parse_boolean(const char *v) _pure_;
int parse_pid(const char *s, pid_t *ret);

#define SAFE_ATO_REFUSE_PLUS_MINUS (1U << 30)
#define SAFE_ATO_REFUSE_LEADING_ZERO (1U << 29)
#define SAFE_ATO_REFUSE_LEADING_WHITESPACE (1U << 28)
#define SAFE_ATO_ALL_FLAGS (SAFE_ATO_REFUSE_PLUS_MINUS|SAFE_ATO_REFUSE_LEADING_ZERO|SAFE_ATO_REFUSE_LEADING_WHITESPACE)
#define SAFE_ATO_MASK_FLAGS(base) ((base) & ~SAFE_ATO_ALL_FLAGS)

int safe_atou_full(const char *s, unsigned base, unsigned *ret_u);
static inline int safe_atou(const char *s, unsigned *ret_u) {
        return safe_atou_full(s, 0, ret_u);
}

int safe_atoi(const char *s, int *ret_i);
int safe_atolli(const char *s, long long *ret_lli);

static inline int safe_atou32_full(const char *s, unsigned base, uint32_t *ret_u) {
        assert_cc(sizeof(uint32_t) == sizeof(unsigned));
        return safe_atou_full(s, base, (unsigned*) ret_u);
}

static inline int safe_atou32(const char *s, uint32_t *ret_u) {
        return safe_atou32_full(s, 0, (unsigned*) ret_u);
}

int safe_atollu_full(const char *s, unsigned base, unsigned long long *ret_llu);

static inline int safe_atou64_full(const char *s, unsigned base, uint64_t *ret_u) {
        assert_cc(sizeof(uint64_t) == sizeof(unsigned long long));
        return safe_atollu_full(s, base, (unsigned long long*) ret_u);
}

static inline int safe_atou64(const char *s, uint64_t *ret_u) {
        return safe_atou64_full(s, 0, ret_u);
}

static inline int safe_atoi64(const char *s, int64_t *ret_i) {
        assert_cc(sizeof(int64_t) == sizeof(long long));
        return safe_atolli(s, (long long*) ret_i);
}

#if LONG_MAX == INT_MAX
static inline int safe_atolu_full(const char *s, unsigned base, unsigned long *ret_u) {
        assert_cc(sizeof(unsigned long) == sizeof(unsigned));
        return safe_atou_full(s, base, (unsigned*) ret_u);
}
static inline int safe_atoli(const char *s, long *ret_u) {
        assert_cc(sizeof(long) == sizeof(int));
        return safe_atoi(s, (int*) ret_u);
}
#else
static inline int safe_atolu_full(const char *s, unsigned base, unsigned long *ret_u) {
        assert_cc(sizeof(unsigned long) == sizeof(unsigned long long));
        return safe_atollu_full(s, base, (unsigned long long*) ret_u);
}
static inline int safe_atoli(const char *s, long *ret_u) {
        assert_cc(sizeof(long) == sizeof(long long));
        return safe_atolli(s, (long long*) ret_u);
}
#endif

static inline int safe_atolu(const char *s, unsigned long *ret_u) {
        return safe_atolu_full(s, 0, ret_u);
}

#if SIZE_MAX == UINT_MAX
static inline int safe_atozu(const char *s, size_t *ret_u) {
        assert_cc(sizeof(size_t) == sizeof(unsigned));
        return safe_atou(s, (unsigned *) ret_u);
}
#else
static inline int safe_atozu(const char *s, size_t *ret_u) {
        assert_cc(sizeof(size_t) == sizeof(unsigned long));
        return safe_atolu(s, ret_u);
}
#endif

int safe_atod(const char *s, double *ret_d);

/* Implement floating point using fixed integers, to improve performance when
 * calculating load averages. These macros can be used to extract the integer
 * and decimal parts of a value. */
#define LOADAVG_PRECISION_BITS  11
#define LOADAVG_FIXED_POINT_1_0 (1 << LOADAVG_PRECISION_BITS)
#define LOADAVG_INT_SIDE(x)     ((x) >> LOADAVG_PRECISION_BITS)
#define LOADAVG_DECIMAL_SIDE(x) LOADAVG_INT_SIDE(((x) & (LOADAVG_FIXED_POINT_1_0 - 1)) * 100)

/* Given a Linux load average (e.g. decimal number 34.89 where 34 is passed as i and 89 is passed as f), convert it
 * to a loadavg_t. */

