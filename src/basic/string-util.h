/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include <string.h>

#include "alloc-util.h"
#include "forward.h"

#include "../fundamental/string-util.h" /* IWYU pragma: export */

static inline char* strstr_ptr_internal(const char *haystack, const char *needle) {
        if (!haystack || !needle)
                return NULL;
        return (char*) strstr(haystack, needle);
}

#define strstr_ptr(haystack, needle) \
        const_generic(haystack, strstr_ptr_internal(haystack, needle))

static inline char* strstrafter_internal(const char *haystack, const char *needle) {
        /* Returns NULL if not found, or pointer to first character after needle if found */

        char *p = (char*) strstr_ptr(haystack, needle);
        if (!p)
                return NULL;

        return p + strlen(needle);
}

#define strstrafter(haystack, needle) \
        const_generic(haystack, strstrafter_internal(haystack, needle))

static inline const char* strnull(const char *s) {
        return s ?: "(null)";
}

static inline const char* strna(const char *s) {
        return s ?: "n/a";
}

static inline const char* true_false(bool b) {
        return b ? "true" : "false";
}

/* This macro's return pointer will have the "const" qualifier set or unset the same way as the input
 * pointer. */
#define empty_to_null(p)                                \
        ({                                              \
                const char *_p = (p);                   \
                (typeof(p)) (isempty(_p) ? NULL : _p);  \
        })

static inline bool empty_or_dash(const char *str) {
        return !str ||
                str[0] == 0 ||
                (str[0] == '-' && str[1] == 0);
}

static inline const char* empty_or_dash_to_null(const char *p) {
        return empty_or_dash(p) ? NULL : p;
}
#define empty_or_dash_to_null(p)                                \
        ({                                                      \
                const char *_p = (p);                           \
                (typeof(p)) (empty_or_dash(_p) ? NULL : _p);    \
        })

char* first_word(const char *s, const char *word) _pure_;

#define strjoin(a, ...) strextend_with_separator_internal(NULL, NULL, a, __VA_ARGS__, NULL)

#define strjoina(a, ...)                                                \
        ({                                                              \
                const char *_appendees_[] = { a, __VA_ARGS__ };         \
                char *_d_, *_p_;                                        \
                size_t _len_ = 0;                                       \
                size_t _i_;                                             \
                for (_i_ = 0; _i_ < ELEMENTSOF(_appendees_) && _appendees_[_i_]; _i_++) \
                        _len_ += strlen(_appendees_[_i_]);              \
                _p_ = _d_ = newa(char, _len_ + 1);                      \
                for (_i_ = 0; _i_ < ELEMENTSOF(_appendees_) && _appendees_[_i_]; _i_++) \
                        _p_ = stpcpy(_p_, _appendees_[_i_]);            \
                *_p_ = 0;                                               \
                _d_;                                                    \
        })

char* strstrip(char *s);
char* delete_trailing_chars(char *s, const char *bad);

static inline char* skip_leading_chars(const char *s, const char *bad) {
        if (!s)
                return NULL;

        if (!bad)
                bad = WHITESPACE;

        return (char*) s + strspn(s, bad);
}

static inline bool _pure_ in_charset(const char *s, const char *charset) {
        assert(s);
        assert(charset);
        return s[strspn(s, charset)] == '\0';
}

static inline bool char_is_cc(char p) {
        /* char is unsigned on some architectures, e.g. aarch64. So, compiler may warn the condition
         * p >= 0 is always true. See #19543. Hence, let's cast to unsigned before the comparison. Note
         * that the cast in the right hand side is redundant, as according to the C standard, compilers
         * automatically cast a signed value to unsigned when comparing with an unsigned variable. Just
         * for safety and readability. */
        return (uint8_t) p < (uint8_t) ' ' || p == 127;
}
bool string_has_cc(const char *p, const char *ok) _pure_;

char* cellescape(char *buf, size_t len, const char *s);

/* This limit is arbitrary, enough to give some idea what the string contains */
#define CELLESCAPE_DEFAULT_LENGTH 64

char* strshorten(char *s, size_t l);

char* strextendv_with_separator(char **x, const char *separator, va_list ap);
char* strextend_with_separator_internal(char **x, const char *separator, ...) _sentinel_;
#define strextend_with_separator(x, separator, ...) strextend_with_separator_internal(x, separator, __VA_ARGS__, NULL)
#define strextend(x, ...) strextend_with_separator_internal(x, NULL, __VA_ARGS__, NULL)

int strextendf_with_separator(char **x, const char *separator, const char *format, ...) _printf_(3,4);
#define strextendf(x, ...) strextendf_with_separator(x, NULL, __VA_ARGS__)

#define strprepend_with_separator(x, separator, ...)                            \
        ({                                                                      \
                char **_p_ = ASSERT_PTR(x), *_s_;                               \
                _s_ = strextend_with_separator_internal(NULL, (separator), __VA_ARGS__, empty_to_null(*_p_), NULL); \
                if (_s_) {                                                      \
                        free(*_p_);                                             \
                        *_p_ = _s_;                                             \
                }                                                               \
                _s_;                                                            \
        })
#define strprepend(x, ...) strprepend_with_separator(x, NULL, __VA_ARGS__)

#define strrepa(s, n)                                                   \
        ({                                                              \
                const char *_sss_ = (s);                                \
                size_t _nnn_ = (n), _len_ = strlen(_sss_);              \
                assert_se(MUL_ASSIGN_SAFE(&_len_, _nnn_));              \
                char *_d_, *_p_;                                        \
                _p_ = _d_ = newa(char, _len_ + 1);                      \
                for (size_t _i_ = 0; _i_ < _nnn_; _i_++)                \
                        _p_ = stpcpy(_p_, _sss_);                       \
                *_p_ = 0;                                               \
                _d_;                                                    \
        })

int free_and_strdup(char **p, const char *s);
int free_and_strndup(char **p, const char *s, size_t l) _nonnull_if_nonzero_(2, 3);

int strdup_to_full(char **ret, const char *src);
static inline int strdup_to(char **ret, const char *src) {
        int r = strdup_to_full(ASSERT_PTR(ret), src);
        return r < 0 ? r : 0;  /* Suppress return value of 1. */
}

typedef enum StringSafeFlags {
        STRING_ASCII               = 1 << 0, /* Verify string is 7-Bit ASCII (rather than just UTF-8) */
        STRING_ALLOW_EMPTY         = 1 << 1, /* Allow empty strings */
        STRING_ALLOW_NEWLINES      = 1 << 2, /* Allow newlines (\n) */
        STRING_ALLOW_BACKSLASHES   = 1 << 3, /* Allow backslashes (\) */
        STRING_ALLOW_QUOTES        = 1 << 4, /* Allow quotes (" or ') */
        STRING_ALLOW_GLOBS         = 1 << 5, /* Allow globs (?, * or [) */
        STRING_FILENAME            = 1 << 6, /* Verify the string is valid as regular filename */
        STRING_FILENAME_PART       = 1 << 7, /* Verify the string is valid as part of a regular filename */
        STRING_DISALLOW_WHITESPACE = 1 << 8, /* Refuse whitespace (space, tab, newline, …) */
} StringSafeFlags;

bool string_is_safe(const char *p, StringSafeFlags flags) _pure_;

DISABLE_WARNING_STRINGOP_TRUNCATION;
REENABLE_WARNING;

/* Like startswith_no_case(), but operates on arbitrary memory blocks.
 * It works only for ASCII strings.
 */

char* str_realloc(char *p);

typedef enum MakeCStringMode {
        MAKE_CSTRING_REFUSE_TRAILING_NUL,
        MAKE_CSTRING_ALLOW_TRAILING_NUL,
        MAKE_CSTRING_REQUIRE_TRAILING_NUL,
        _MAKE_CSTRING_MODE_MAX,
        _MAKE_CSTRING_MODE_INVALID = -1,
} MakeCStringMode;

int make_cstring(const void *s, size_t n, MakeCStringMode mode, char **ret);

char* strdupcspn(const char *a, const char *reject);

/* These are like strdupa()/strndupa(), but honour ALLOCA_MAX */
#define strdupa_safe(s)                                                 \
        ({                                                              \
                const char *_t = (s);                                   \
                (char*) memdupa_suffix0(_t, strlen(_t));                \
        })

#define strndupa_safe(s, n)                                             \
        ({                                                              \
                const char *_t = (s);                                   \
                (char*) memdupa_suffix0(_t, strnlen(_t, n));            \
        })
