/* SPDX-License-Identifier: LGPL-2.1-or-later */


#include "escape.h"
#include "glyph-util.h"
#include "log.h"
#include "path-util.h"
#include "string-util.h"
#include "utf8.h"

char* first_word(const char *s, const char *word) {
        assert(s);
        assert(word);

        /* Checks if the string starts with the specified word, either followed by NUL or by whitespace.
         * Returns a pointer to the NUL or the first character after the whitespace. */

        if (isempty(word))
                return (char*) s;

        const char *p = startswith(s, word);
        if (!p)
                return NULL;
        if (*p == '\0')
                return (char*) p;

        const char *nw = skip_leading_chars(p, WHITESPACE);
        if (p == nw)
                return NULL;

        return (char*) nw;
}

char* strstrip(char *s) {
        if (!s)
                return NULL;

        /* Drops trailing whitespace. Modifies the string in place. Returns pointer to first non-space character */

        return delete_trailing_chars(skip_leading_chars(s, WHITESPACE), WHITESPACE);
}

char* delete_trailing_chars(char *s, const char *bad) {
        char *c = s;

        /* Drops all specified bad characters, at the end of the string */

        if (!s)
                return NULL;

        if (!bad)
                bad = WHITESPACE;

        for (char *p = s; *p; p++)
                if (!strchr(bad, *p))
                        c = p + 1;

        *c = 0;

        return s;
}

bool string_has_cc(const char *p, const char *ok) {
        assert(p);

        /*
         * Check if a string contains control characters. If 'ok' is
         * non-NULL it may be a string containing additional CCs to be
         * considered OK.
         */

        for (const char *t = p; *t; t++) {
                if (ok && strchr(ok, *t))
                        continue;

                if (char_is_cc(*t))
                        return true;
        }

        return false;
}

static int write_ellipsis(char *buf, bool unicode) {
        const char *s = glyph_full(GLYPH_ELLIPSIS, unicode);
        assert(strlen(s) == 3);
        memcpy(buf, s, 3);
        return 3;
}

/* Walk backwards from 'end' (exclusive) to the start of the last complete UTF-8 character, without
 * descending below 'start', validate it and return a pointer to its first byte, optionally decoding it
 * into *ret_c. Returns NULL on a truncated or otherwise malformed sequence. */
char* cellescape(char *buf, size_t len, const char *s) {
        /* Escape and ellipsize s into buffer buf of size len. Only non-control ASCII
         * characters are copied as they are, everything else is escaped. The result
         * is different then if escaping and ellipsization was performed in two
         * separate steps, because each sequence is either stored in full or skipped.
         *
         * This function should be used for logging about strings which expected to
         * be plain ASCII in a safe way.
         *
         * An ellipsis will be used if s is too long. It was always placed at the
         * very end.
         */

        size_t i = 0, last_char_width[4] = {}, k = 0;

        assert(buf);
        assert(len > 0); /* at least a terminating NUL */
        assert(s);

        for (;;) {
                char four[4];
                int w;

                if (*s == 0) /* terminating NUL detected? then we are done! */
                        goto done;

                w = cescape_char(*s, four);
                if (i + w + 1 > len) /* This character doesn't fit into the buffer anymore? In that case let's
                                      * ellipsize at the previous location */
                        break;

                /* OK, there was space, let's add this escaped character to the buffer */
                memcpy(buf + i, four, w);
                i += w;

                /* And remember its width in the ring buffer */
                last_char_width[k] = w;
                k = (k + 1) % 4;

                s++;
        }

        /* Ellipsation is necessary. This means we might need to truncate the string again to make space for 4
         * characters ideally, but the buffer is shorter than that in the first place take what we can get */
        for (size_t j = 0; j < ELEMENTSOF(last_char_width); j++) {

                if (i + 4 <= len) /* nice, we reached our space goal */
                        break;

                k = k == 0 ? 3 : k - 1;
                if (last_char_width[k] == 0) /* bummer, we reached the beginning of the strings */
                        break;

                assert(i >= last_char_width[k]);
                i -= last_char_width[k];
        }

        if (i + 4 <= len) /* yay, enough space */
                i += write_ellipsis(buf + i, /* unicode= */ false);
        else if (i + 3 <= len) { /* only space for ".." */
                buf[i++] = '.';
                buf[i++] = '.';
        } else if (i + 2 <= len) /* only space for a single "." */
                buf[i++] = '.';
        else
                assert(i + 1 <= len);

done:
        buf[i] = '\0';
        return buf;
}

char* strshorten(char *s, size_t l) {
        assert(s);

        if (l >= SIZE_MAX-1) /* Would not change anything */
                return s;

        if (strnlen(s, l+1) > l)
                s[l] = 0;

        return s;
}

char* strextendv_with_separator(char **x, const char *separator, va_list ap) {
        _cleanup_free_ char *buffer = NULL;
        size_t f, l, l_separator;
        bool need_separator;
        char *nr, *p;

        if (!x)
                x = &buffer;

        l = f = strlen_ptr(*x);

        need_separator = !isempty(*x);
        l_separator = strlen_ptr(separator);

        va_list aq;
        va_copy(aq, ap);
        for (const char *t;;) {
                size_t n;

                t = va_arg(aq, const char *);
                if (!t)
                        break;
                if (t == POINTER_MAX)
                        continue;

                n = strlen(t);

                if (need_separator)
                        n += l_separator;

                if (n >= SIZE_MAX - l) {
                        va_end(aq);
                        return NULL;
                }

                l += n;
                need_separator = true;
        }
        va_end(aq);

        need_separator = !isempty(*x);

        nr = realloc(*x, GREEDY_ALLOC_ROUND_UP(l+1));
        if (!nr)
                return NULL;

        *x = nr;
        p = nr + f;

        for (;;) {
                const char *t;

                t = va_arg(ap, const char *);
                if (!t)
                        break;
                if (t == POINTER_MAX)
                        continue;

                if (need_separator && separator)
                        p = stpcpy(p, separator);

                p = stpcpy(p, t);

                need_separator = true;
        }

        assert(p == nr + l);
        *p = 0;

        /* If no buffer to extend was passed in return the start of the buffer */
        if (buffer)
                return TAKE_PTR(buffer);

        /* Otherwise we extended the buffer: return the end */
        return p;
}

char* strextend_with_separator_internal(char **x, const char *separator, ...) {
        va_list ap;
        char *ret;

        va_start(ap, separator);
        ret = strextendv_with_separator(x, separator, ap);
        va_end(ap);

        return ret;
}

int strextendf_with_separator(char **x, const char *separator, const char *format, ...) {
        size_t m, a, l_separator;
        va_list ap;
        int l;

        /* Appends a formatted string to the specified string. Don't use this in inner loops, since then
         * we'll spend a tonload of time in determining the length of the string passed in, over and over
         * again. */

        assert(x);
        assert(format);

        l_separator = isempty(*x) ? 0 : strlen_ptr(separator);

        /* Let's try to use the allocated buffer, if there's room at the end still. Otherwise let's extend by 64 chars. */
        if (*x) {
                m = strlen(*x);
                a = MALLOC_SIZEOF_SAFE(*x);
                assert(a >= m + 1);
        } else
                m = a = 0;

        if (a - m < 17 + l_separator) { /* if there's less than 16 chars space, then enlarge the buffer first */
                char *n;

                if (_unlikely_(l_separator > SIZE_MAX - 64)) /* overflow check #1 */
                        return -ENOMEM;
                if (_unlikely_(m > SIZE_MAX - 64 - l_separator)) /* overflow check #2 */
                        return -ENOMEM;

                n = realloc(*x, m + 64 + l_separator);
                if (!n)
                        return -ENOMEM;

                *x = n;
                a = MALLOC_SIZEOF_SAFE(*x);
        }

        /* Now, let's try to format the string into it */
        memcpy_safe(*x + m, separator, l_separator);
        va_start(ap, format);
        l = vsnprintf(*x + m + l_separator, a - m - l_separator, format, ap);
        va_end(ap);

        assert(l >= 0);

        if ((size_t) l < a - m - l_separator) {
                char *n;

                /* Nice! This worked. We are done. But first, let's return the extra space we don't
                 * need. This should be a cheap operation, since we only lower the allocation size here,
                 * never increase. */
                n = realloc(*x, m + (size_t) l + l_separator + 1);
                if (n)
                        *x = n;
        } else {
                char *n;

                /* Wasn't enough. Then let's allocate exactly what we need. */

                if (_unlikely_((size_t) l > SIZE_MAX - (l_separator + 1))) /* overflow check #1 */
                        goto oom;
                if (_unlikely_(m > SIZE_MAX - ((size_t) l + l_separator + 1))) /* overflow check #2 */
                        goto oom;

                a = m + (size_t) l + l_separator + 1;
                n = realloc(*x, a);
                if (!n)
                        goto oom;
                *x = n;

                va_start(ap, format);
                l = vsnprintf(*x + m + l_separator, a - m - l_separator, format, ap);
                va_end(ap);

                assert((size_t) l < a - m - l_separator);
        }

        return 0;

oom:
        /* truncate the bytes added after memcpy_safe() again */
        (*x)[m] = 0;
        return -ENOMEM;
}

int free_and_strdup(char **p, const char *s) {
        char *t;

        assert(p);

        /* Replaces a string pointer with a strdup()ed new string,
         * possibly freeing the old one. */

        if (streq_ptr(*p, s))
                return 0;

        if (s) {
                t = strdup(s);
                if (!t)
                        return -ENOMEM;
        } else
                t = NULL;

        free_and_replace(*p, t);

        return 1;
}

int free_and_strndup(char **p, const char *s, size_t l) {
        char *t;

        assert(p);
        assert(s || l == 0);

        /* Replaces a string pointer with a strndup()ed new string,
         * freeing the old one. */

        if (!*p && !s)
                return 0;

        if (*p && s && strneq(*p, s, l) && (l > strlen(*p) || (*p)[l] == '\0'))
                return 0;

        if (s) {
                t = strndup(s, l);
                if (!t)
                        return -ENOMEM;
        } else
                t = NULL;

        free_and_replace(*p, t);
        return 1;
}

int strdup_to_full(char **ret, const char *src) {
        if (!src) {
                if (ret)
                        *ret = NULL;

                return 0;
        } else {
                if (ret) {
                        char *t = strdup(src);
                        if (!t)
                                return -ENOMEM;
                        *ret = t;
                }

                return 1;
        }
};

bool string_is_safe(const char *p, StringSafeFlags flags) {

        /* Baseline checks are:
         *   • No control characters (i.e. 0…31 + 127)
         *   • UTF-8 valid (well, technically we skip this test if STRING_ASCII is set, since that is a tighter test)
         */

        if (FLAGS_SET(flags, STRING_ALLOW_EMPTY) ? !p : isempty(p))
                return false;

        if (!FLAGS_SET(flags, STRING_ASCII) && !utf8_is_valid(p))
                return false;

        for (const char *t = p; *t; t++) {
                /* never allow control characters, except for new line */
                if ((*t > 0 && *t < ' ' && *t != '\n') || *t == 0x7f)
                        return false;

                if (!FLAGS_SET(flags, STRING_ALLOW_NEWLINES) && *t == '\n')
                        return false;

                if (!FLAGS_SET(flags, STRING_ALLOW_BACKSLASHES) && *t == '\\')
                        return false;

                if (!FLAGS_SET(flags, STRING_ALLOW_QUOTES) && strchr(QUOTES, *t))
                        return false;

                if (!FLAGS_SET(flags, STRING_ALLOW_GLOBS) && strchr(GLOB_CHARS, *t))
                        return false;

                if (FLAGS_SET(flags, STRING_DISALLOW_WHITESPACE) && strchr(WHITESPACE, *t))
                        return false;

                if (FLAGS_SET(flags, STRING_ASCII) && (uint8_t) *t >= 0x80)
                        return false;
        }

        if (FLAGS_SET(flags, STRING_FILENAME) && !filename_is_valid(p))
                return false;

        if (FLAGS_SET(flags, STRING_FILENAME_PART) && !filename_part_is_valid(p))
                return false;

        return true;
}

char* str_realloc(char *p) {
        /* Reallocate *p to actual size. Ignore failure, and return the original string on error. */

        if (!p)
                return NULL;

        return realloc(p, strlen(p) + 1) ?: p;
}

int make_cstring(const void *s, size_t n, MakeCStringMode mode, char **ret) {
        char *b;

        assert(s || n == 0);
        assert(mode >= 0);
        assert(mode < _MAKE_CSTRING_MODE_MAX);

        /* Converts a sized character buffer into a NUL-terminated NUL string, refusing if there are embedded
         * NUL bytes. Whether to expect a trailing NUL byte can be specified via 'mode' */

        if (n == 0) {
                if (mode == MAKE_CSTRING_REQUIRE_TRAILING_NUL)
                        return -EINVAL;

                if (!ret)
                        return 0;

                b = new0(char, 1);
        } else {
                const uint8_t *nul;

                nul = memchr(s, 0, n);
                if (nul) {
                        if (nul < (const uint8_t*) s + n - 1 || /* embedded NUL? */
                            mode == MAKE_CSTRING_REFUSE_TRAILING_NUL)
                                return -EINVAL;

                        n--;
                } else if (mode == MAKE_CSTRING_REQUIRE_TRAILING_NUL)
                        return -EINVAL;

                if (!ret)
                        return 0;

                b = memdup_suffix0(s, n);
        }
        if (!b)
                return -ENOMEM;

        *ret = b;
        return 0;
}

char* strdupcspn(const char *a, const char *reject) {
        if (isempty(a))
                return strdup("");
        if (isempty(reject))
                return strdup(a);

        return strndup(a, strcspn(a, reject));
}

