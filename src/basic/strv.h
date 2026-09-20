/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include "forward.h"

#include "../fundamental/strv.h"   /* IWYU pragma: export */

char* strv_find(char * const *l, const char *name) _pure_;
char* strv_find_case(char * const *l, const char *name) _pure_;

#define strv_contains(l, s) (!!strv_find((l), (s)))
#define strv_contains_case(l, s) (!!strv_find_case((l), (s)))

char** strv_free(char **l);
DEFINE_TRIVIAL_CLEANUP_FUNC(char**, strv_free);
#define _cleanup_strv_free_ _cleanup_(strv_freep)

char** strv_free_erase(char **l);
DEFINE_TRIVIAL_CLEANUP_FUNC(char**, strv_free_erase);
#define _cleanup_strv_free_erase_ _cleanup_(strv_free_erasep)

char** strv_copy_n(char * const *l, size_t n);
static inline char** strv_copy(char * const *l) {
        return strv_copy_n(l, SIZE_MAX);
}

size_t strv_length(char * const *l) _pure_;

int strv_extend_strv(char ***a, char * const *b, bool filter_duplicates);
int strv_extend_strv_consume(char ***a, char **b, bool filter_duplicates);

/* _with_size() are lower-level functions where the size can be provided externally,
 * which allows us to skip iterating over the strv to find the end, which saves
 * a bit of time and reduces the complexity of appending from O(n²) to O(n). */

int strv_extend_with_size(char ***l, size_t *n, const char *value);
static inline int strv_extend(char ***l, const char *value) {
        return strv_extend_with_size(l, NULL, value);
}

int strv_extend_many_internal(char ***l, const char *value, ...);
#define strv_extend_many(l, ...) strv_extend_many_internal(l, __VA_ARGS__, POINTER_MAX)

int strv_extendf_with_size(char ***l, size_t *n, const char *format, ...) _printf_(3,4);
#define strv_extendf(l, ...) strv_extendf_with_size(l, NULL, __VA_ARGS__)

int strv_push_with_size(char ***l, size_t *n, char *value);

int strv_insert(char ***l, size_t position, char *value);

static inline int strv_push_prepend(char ***l, char *value) {
        return strv_insert(l, 0, value);
}

int strv_consume_with_size(char ***l, size_t *n, char *value);
static inline int strv_consume(char ***l, char *value) {
        return strv_consume_with_size(l, NULL, value);
}

int strv_consume_prepend(char ***l, char *value);

char** strv_remove(char **l, const char *s);
char** strv_uniq(char **l);

int strv_compare(char * const *a, char * const *b) _pure_;
static inline bool strv_equal(char * const *a, char * const *b) {
        return strv_compare(a, b) == 0;
}

char** strv_new_internal(const char *x, ...) _sentinel_;
char** strv_new_ap(const char *x, va_list ap);
#define strv_new(...) strv_new_internal(__VA_ARGS__, NULL)

#define STRV_IGNORE ((const char *) POINTER_MAX)

static inline const char* STRV_IFNOTNULL(const char *x) {
        return x ?: STRV_IGNORE;
}

int strv_split_full(char ***t, const char *s, const char *separators, ExtractFlags flags);
char** strv_split(const char *s, const char *separators);

/* Given a string containing white-space separated tuples of words themselves separated by ':',
 * returns a vector of strings. If the second element in a tuple is missing, the corresponding
 * string in the vector is an empty string. */

char* strv_join_full(char * const *l, const char *separator, const char *prefix);
static inline char *strv_join(char * const *l, const char *separator) {
        return strv_join_full(l, separator, NULL);
}

#define _STRV_FOREACH_BACKWARDS(s, l, h, i)                             \
        for (typeof(*(l)) *s, *h = (l), *i = ({                         \
                                size_t _len = strv_length(h);           \
                                _len > 0 ? h + _len - 1 : NULL;         \
                        });                                             \
             (s = i);                                                   \
             i = PTR_SUB1(i, h))

#define STRV_FOREACH_BACKWARDS(s, l)                                    \
        _STRV_FOREACH_BACKWARDS(s, l, UNIQ_T(h, UNIQ), UNIQ_T(i, UNIQ))

#define _STRV_FOREACH_PAIR(x, y, l, i)                          \
        for (typeof(*(l)) *x, *y, *i = (l);                     \
             i && *(x = i) && *(y = i + 1);                     \
             i += 2)

#define STRV_FOREACH_PAIR(x, y, l)                      \
        _STRV_FOREACH_PAIR(x, y, l, UNIQ_T(i, UNIQ))

char** strv_sort(char **l);

char* startswith_strv_internal(const char *s, char * const *l);
#define startswith_strv(s, l) const_generic(s, startswith_strv_internal(s, l))

#define STARTSWITH_SET(p, ...)                                  \
        startswith_strv(p, STRV_MAKE(__VA_ARGS__))

char* endswith_strv_internal(const char *s, char * const *l);
#define endswith_strv(s, l) const_generic(s, endswith_strv_internal(s, l))

#define ENDSWITH_SET(p, ...)                                    \
        endswith_strv(p, STRV_MAKE(__VA_ARGS__))

#define STR_IN_SET(x, ...) strv_contains(STRV_MAKE(__VA_ARGS__), x)
#define STRPTR_IN_SET(x, ...)                                    \
        ({                                                       \
                const char* _x = (x);                            \
                _x && strv_contains(STRV_MAKE(__VA_ARGS__), _x); \
        })

#define STRCASE_IN_SET(x, ...) strv_contains_case(STRV_MAKE(__VA_ARGS__), x)
#define STRCASEPTR_IN_SET(x, ...)                                    \
        ({                                                       \
                const char* _x = (x);                            \
                _x && strv_contains_case(STRV_MAKE(__VA_ARGS__), _x); \
        })

#define _FOREACH_STRING(uniq, x, y, ...)                                \
        for (const char *x, * const*UNIQ_T(l, uniq) = STRV_MAKE_CONST(({ x = y; }), ##__VA_ARGS__); \
             x;                                                         \
             x = *(++UNIQ_T(l, uniq)))

#define FOREACH_STRING(x, y, ...)                       \
        _FOREACH_STRING(UNIQ, x, y, ##__VA_ARGS__)

int strv_extend_n(char ***l, const char *value, size_t n);

int strv_extend_assignment(char ***l, const char *lhs, const char *rhs);

#define strv_free_and_replace(a, b)             \
        free_and_replace_full(a, b, strv_free)

int strv_rebreak_lines(char **l, size_t width, char ***ret);

/* whenever we need to initialize something with a constant non-NULL, but empty strv, we can use this shared
 * one */
extern const char* const strv_empty[1];

#define STRV_EMPTY ((char**) strv_empty)
