/* SPDX-License-Identifier: LGPL-2.1-or-later */


#include "escape.h"
#include "hexdecoct.h"
#include "string-util.h"
#include "strv.h"
#include "utf8.h"

int cescape_char(char c, char *buf) {
        char *buf_old = buf;

        /* Needs space for 4 characters in the buffer */

        switch (c) {

                case '\a':
                        *(buf++) = '\\';
                        *(buf++) = 'a';
                        break;
                case '\b':
                        *(buf++) = '\\';
                        *(buf++) = 'b';
                        break;
                case '\f':
                        *(buf++) = '\\';
                        *(buf++) = 'f';
                        break;
                case '\n':
                        *(buf++) = '\\';
                        *(buf++) = 'n';
                        break;
                case '\r':
                        *(buf++) = '\\';
                        *(buf++) = 'r';
                        break;
                case '\t':
                        *(buf++) = '\\';
                        *(buf++) = 't';
                        break;
                case '\v':
                        *(buf++) = '\\';
                        *(buf++) = 'v';
                        break;
                case '\\':
                        *(buf++) = '\\';
                        *(buf++) = '\\';
                        break;
                case '"':
                        *(buf++) = '\\';
                        *(buf++) = '"';
                        break;
                case '\'':
                        *(buf++) = '\\';
                        *(buf++) = '\'';
                        break;

                default:
                        /* For special chars we prefer octal over
                         * hexadecimal encoding, simply because glib's
                         * g_strescape() does the same */
                        if ((c < ' ') || (c >= 127)) {
                                *(buf++) = '\\';
                                *(buf++) = octchar((unsigned char) c >> 6);
                                *(buf++) = octchar((unsigned char) c >> 3);
                                *(buf++) = octchar((unsigned char) c);
                        } else
                                *(buf++) = c;
                        break;
        }

        return buf - buf_old;
}

char* cescape_length(const char *s, size_t n) {
        const char *f;
        char *r, *t;

        /* Does C style string escaping. May be reversed with cunescape(). */

        assert(s || n == 0);

        if (n == SIZE_MAX)
                n = strlen(s);

        if (n > (SIZE_MAX - 1) / 4)
                return NULL;

        r = new(char, n*4 + 1);
        if (!r)
                return NULL;

        for (f = s, t = r; f < s + n; f++)
                t += cescape_char(*f, t);

        *t = 0;

        return r;
}

int cunescape_one(const char *p, size_t length, char32_t *ret, bool *eight_bit, bool accept_nul) {
        int r = 1;

        assert(p);
        assert(ret);
        assert(eight_bit);

        /* Unescapes C style. Returns the unescaped character in ret.
         * Sets *eight_bit to true if the escaped sequence either fits in
         * one byte in UTF-8 or is a non-unicode literal byte and should
         * instead be copied directly.
         */

        if (length != SIZE_MAX && length < 1)
                return -EINVAL;

        switch (p[0]) {

        case 'a':
                *ret = '\a';
                break;
        case 'b':
                *ret = '\b';
                break;
        case 'f':
                *ret = '\f';
                break;
        case 'n':
                *ret = '\n';
                break;
        case 'r':
                *ret = '\r';
                break;
        case 't':
                *ret = '\t';
                break;
        case 'v':
                *ret = '\v';
                break;
        case '\\':
                *ret = '\\';
                break;
        case '"':
                *ret = '"';
                break;
        case '\'':
                *ret = '\'';
                break;

        case 's':
                /* This is an extension of the XDG syntax files */
                *ret = ' ';
                break;

        case 'x': {
                /* hexadecimal encoding */
                int a, b;

                if (length != SIZE_MAX && length < 3)
                        return -EINVAL;

                a = unhexchar(p[1]);
                if (a < 0)
                        return -EINVAL;

                b = unhexchar(p[2]);
                if (b < 0)
                        return -EINVAL;

                /* Don't allow NUL bytes */
                if (a == 0 && b == 0 && !accept_nul)
                        return -EINVAL;

                *ret = (a << 4U) | b;
                *eight_bit = true;
                r = 3;
                break;
        }

        case 'u': {
                /* C++11 style 16-bit unicode */

                int a[4];
                size_t i;
                uint32_t c;

                if (length != SIZE_MAX && length < 5)
                        return -EINVAL;

                for (i = 0; i < 4; i++) {
                        a[i] = unhexchar(p[1 + i]);
                        if (a[i] < 0)
                                return a[i];
                }

                c = ((uint32_t) a[0] << 12U) | ((uint32_t) a[1] << 8U) | ((uint32_t) a[2] << 4U) | (uint32_t) a[3];

                /* Don't allow 0 chars */
                if (c == 0 && !accept_nul)
                        return -EINVAL;

                /* Don't allow UTF-16 surrogates, they cannot be encoded as valid UTF-8. Note we
                 * deliberately do *not* use unichar_is_valid() here (unlike the \U case below):
                 * it also rejects noncharacters such as U+FFFE, which callers legitimately round-trip
                 * through \u (e.g. systemd.mount-extra= parsing, see test-fstab-generator). */
                if (utf16_is_surrogate(c))
                        return -EINVAL;

                *ret = c;
                r = 5;
                break;
        }

        case 'U': {
                /* C++11 style 32-bit unicode */

                int a[8];
                size_t i;
                char32_t c;

                if (length != SIZE_MAX && length < 9)
                        return -EINVAL;

                for (i = 0; i < 8; i++) {
                        a[i] = unhexchar(p[1 + i]);
                        if (a[i] < 0)
                                return a[i];
                }

                c = ((uint32_t) a[0] << 28U) | ((uint32_t) a[1] << 24U) | ((uint32_t) a[2] << 20U) | ((uint32_t) a[3] << 16U) |
                    ((uint32_t) a[4] << 12U) | ((uint32_t) a[5] <<  8U) | ((uint32_t) a[6] <<  4U) |  (uint32_t) a[7];

                /* Don't allow 0 chars */
                if (c == 0 && !accept_nul)
                        return -EINVAL;

                /* Don't allow invalid code points */
                if (!unichar_is_valid(c))
                        return -EINVAL;

                *ret = c;
                r = 9;
                break;
        }

        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7': {
                /* octal encoding */
                int a, b, c;
                char32_t m;

                if (length != SIZE_MAX && length < 3)
                        return -EINVAL;

                a = unoctchar(p[0]);
                if (a < 0)
                        return -EINVAL;

                b = unoctchar(p[1]);
                if (b < 0)
                        return -EINVAL;

                c = unoctchar(p[2]);
                if (c < 0)
                        return -EINVAL;

                /* don't allow NUL bytes */
                if (a == 0 && b == 0 && c == 0 && !accept_nul)
                        return -EINVAL;

                /* Don't allow bytes above 255 */
                m = ((uint32_t) a << 6U) | ((uint32_t) b << 3U) | (uint32_t) c;
                if (m > 255)
                        return -EINVAL;

                *ret = m;
                *eight_bit = true;
                r = 3;
                break;
        }

        default:
                return -EINVAL;
        }

        return r;
}

char* octescape_full(const char *s, size_t len, const char *bad) {
        char *buf, *t;

        /* Escapes all chars in bad, in addition to \ and " chars, in \nnn octal style escaping. May be
         * reversed with cunescape(). */

        assert(s || len == 0);

        if (len == SIZE_MAX)
                len = strlen(s);

        if (len > (SIZE_MAX - 1) / 4)
                return NULL;

        t = buf = new(char, len * 4 + 1);
        if (!buf)
                return NULL;

        for (size_t i = 0; i < len; i++) {
                uint8_t u = (uint8_t) s[i];

                if (u < ' ' || u >= 127 || IN_SET(u, '\\', '"') || (bad && strchr(bad, u))) {
                        *(t++) = '\\';
                        *(t++) = '0' + (u >> 6);
                        *(t++) = '0' + ((u >> 3) & 7);
                        *(t++) = '0' + (u & 7);
                } else
                        *(t++) = u;
        }

        *t = 0;
        return buf;
}

static char* strcpy_backslash_escaped(char *t, const char *s, const char *bad) {
        assert(bad);
        assert(t);
        assert(s);

        while (*s) {
                int l = utf8_encoded_valid_unichar(s, SIZE_MAX);

                if (char_is_cc(*s) || l < 0)
                        t += cescape_char(*(s++), t);
                else if (l == 1) {
                        if (*s == '\\' || strchr(bad, *s))
                                *(t++) = '\\';
                        *(t++) = *(s++);
                } else {
                        t = mempcpy(t, s, l);
                        s += l;
                }
        }

        return t;
}

char* shell_escape(const char *s, const char *bad) {
        char *buf, *t;

        buf = new(char, strlen(s)*4+1);
        if (!buf)
                return NULL;

        t = strcpy_backslash_escaped(buf, s, bad);
        *t = 0;

        return buf;
}

char* shell_maybe_quote(const char *s, ShellEscapeFlags flags) {
        const char *p;
        char *buf, *t;

        assert(s);

        /* Encloses a string in quotes if necessary to make it OK as a shell string. */

        if (FLAGS_SET(flags, SHELL_ESCAPE_EMPTY) && isempty(s))
                return strdup("\"\""); /* We don't use $'' here in the POSIX mode. "" is fine too. */

        for (p = s; *p; ) {
                int l = utf8_encoded_valid_unichar(p, SIZE_MAX);

                if (char_is_cc(*p) || l < 0 ||
                    strchr(WHITESPACE SHELL_NEED_QUOTES, *p))
                        break;

                p += l;
        }

        if (!*p)
                return strdup(s);

        buf = new(char, FLAGS_SET(flags, SHELL_ESCAPE_POSIX) + 1 + strlen(s)*4 + 1 + 1);
        if (!buf)
                return NULL;

        t = buf;
        if (FLAGS_SET(flags, SHELL_ESCAPE_POSIX)) {
                *(t++) = '$';
                *(t++) = '\'';
        } else
                *(t++) = '"';

        t = mempcpy(t, s, p - s);

        t = strcpy_backslash_escaped(t, p,
                                     FLAGS_SET(flags, SHELL_ESCAPE_POSIX) ? SHELL_NEED_ESCAPE_POSIX : SHELL_NEED_ESCAPE);

        if (FLAGS_SET(flags, SHELL_ESCAPE_POSIX))
                *(t++) = '\'';
        else
                *(t++) = '"';
        *t = 0;

        return str_realloc(buf);
}

char* quote_command_line(char * const *argv, ShellEscapeFlags flags) {
        _cleanup_free_ char *result = NULL;

        assert(argv);

        STRV_FOREACH(a, argv) {
                _cleanup_free_ char *t = NULL;

                t = shell_maybe_quote(*a, flags);
                if (!t)
                        return NULL;

                if (!strextend_with_separator(&result, " ", t))
                        return NULL;
        }

        return str_realloc(TAKE_PTR(result));
}
