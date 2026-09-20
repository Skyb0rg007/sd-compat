/* SPDX-License-Identifier: LGPL-2.1-or-later */


#include <unistd.h>

#include "ansi-color.h"
#include "devnum-util.h"
#include "log.h"
#include "path-util.h"
#include "process-util.h"
#include "stat-util.h"
#include "terminal-util.h"

/* How much to wait when reading/writing ANSI sequences from/to the console */
#define CONSOLE_ANSI_SEQUENCE_TIMEOUT_USEC (333 * USEC_PER_MSEC)

static volatile unsigned cached_columns = 0;
static volatile unsigned cached_lines = 0;

static volatile int cached_on_tty = -1;
static volatile int cached_on_dev_null = -1;

bool isatty_safe(int fd) {
        assert(fd >= 0);

        if (isatty(fd))
                return true;

        /* Linux/glibc returns EIO for hung up TTY on isatty(). Which is wrong, the thing doesn't stop being
         * a TTY after all, just because it is temporarily hung up. Let's work around this here, until this
         * is fixed in glibc. See: https://sourceware.org/bugzilla/show_bug.cgi?id=32103 */
        if (errno == EIO)
                return true;

        /* Be resilient if we're working on stdio, since they're set up by parent process. */
        assert(errno != EBADF || IN_SET(fd, STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO));

        return false;
}

#define DEFAULT_ASK_REFRESH_USEC (2*USEC_PER_SEC)

typedef enum CompletionResult{
        COMPLETION_ALREADY,       /* the input string is already complete */
        COMPLETION_FULL,          /* completed the input string to be complete now */
        COMPLETION_PARTIAL,       /* completed the input string so that is still incomplete */
        COMPLETION_NONE,          /* found no matching completion */
        _COMPLETION_RESULT_MAX,
        _COMPLETION_RESULT_INVALID = -EINVAL,
        _COMPLETION_RESULT_ERRNO_MAX = -ERRNO_MAX,
} CompletionResult;

/* intended to be used as a SIGWINCH sighandler */
bool on_tty(void) {

        /* We check both stdout and stderr, so that situations where pipes on the shell are used are reliably
         * recognized, regardless if only the output or the errors are piped to some place. Since on_tty() is generally
         * used to default to a safer, non-interactive, non-color mode of operation it's probably good to be defensive
         * here, and check for both. Note that we don't check for STDIN_FILENO, because it should fine to use fancy
         * terminal functionality when outputting stuff, even if the input is piped to us. */

        if (cached_on_tty < 0)
                cached_on_tty =
                        isatty_safe(STDOUT_FILENO) &&
                        isatty_safe(STDERR_FILENO);

        return cached_on_tty;
}

int get_ctty_devnr(pid_t pid, dev_t *ret) {
        _cleanup_free_ char *line = NULL;
        unsigned long ttynr;
        const char *p;
        int r;

        assert(pid >= 0);

        p = procfs_file_alloca(pid, "stat");
        r = read_one_line_file(p, &line);
        if (r < 0)
                return r;

        p = strrchr(line, ')');
        if (!p)
                return -EIO;

        p++;

        if (sscanf(p, " "
                   "%*c "  /* state */
                   "%*d "  /* ppid */
                   "%*d "  /* pgrp */
                   "%*d "  /* session */
                   "%lu ", /* ttynr */
                   &ttynr) != 1)
                return -EIO;

        if (devnum_is_zero(ttynr))
                return -ENXIO;

        if (ret)
                *ret = (dev_t) ttynr;

        return 0;
}

int get_ctty(pid_t pid, dev_t *ret_devnr, char **ret) {
        char pty[STRLEN("/dev/pts/") + DECIMAL_STR_MAX(dev_t) + 1];
        _cleanup_free_ char *buf = NULL;
        const char *fn = NULL, *w;
        dev_t devnr;
        int r;

        r = get_ctty_devnr(pid, &devnr);
        if (r < 0)
                return r;

        r = device_path_make_canonical(S_IFCHR, devnr, &buf);
        if (r < 0) {
                struct stat st;

                if (r != -ENOENT) /* No symlink for this in /dev/char/? */
                        return r;

                /* Maybe this is PTY? PTY devices are not listed in /dev/char/, as they don't follow the
                 * Linux device model and hence device_path_make_canonical() doesn't work for them. Let's
                 * assume this is a PTY for a moment, and check if the device node this would then map to in
                 * /dev/pts/ matches the one we are looking for. This way we don't have to hardcode the major
                 * number (which is 136 btw), but we still rely on the fact that PTY numbers map directly to
                 * the minor number of the pty. */
                xsprintf(pty, "/dev/pts/%u", minor(devnr));

                if (stat(pty, &st) < 0) {
                        if (errno != ENOENT)
                                return -errno;

                } else if (S_ISCHR(st.st_mode) && devnr == st.st_rdev) /* Bingo! */
                        fn = pty;

                if (!fn) {
                        /* Doesn't exist, or not a PTY? Probably something similar to the PTYs which have no
                         * symlink in /dev/char/. Let's return something vaguely useful. */
                        r = device_path_make_major_minor(S_IFCHR, devnr, &buf);
                        if (r < 0)
                                return r;

                        fn = buf;
                }
        } else
                fn = buf;

        w = path_startswith(fn, "/dev/");
        if (!w)
                return -EINVAL;

        if (ret) {
                r = strdup_to(ret, w);
                if (r < 0)
                        return r;
        }

        if (ret_devnr)
                *ret_devnr = devnr;

        return 0;
}

static bool on_dev_null(void) {
        struct stat dst, ost, est;

        if (cached_on_dev_null >= 0)
                return cached_on_dev_null;

        if (stat("/dev/null", &dst) < 0 || fstat(STDOUT_FILENO, &ost) < 0 || fstat(STDERR_FILENO, &est) < 0)
                cached_on_dev_null = false;
        else
                cached_on_dev_null = stat_inode_same(&dst, &ost) && stat_inode_same(&dst, &est);

        return cached_on_dev_null;
}

bool getenv_terminal_is_dumb(void) {
        const char *e;

        e = getenv("TERM");
        if (!e)
                return true;

        return streq(e, "dumb");
}

bool terminal_is_dumb(void) {
        if (!on_tty() && !on_dev_null())
                return true;

        return getenv_terminal_is_dumb();
}

void get_log_colors(int priority, const char **on, const char **off, const char **highlight) {
        /* Note that this will initialize output variables only when there's something to output.
         * The caller must pre-initialize to "" or NULL as appropriate. */

        if (priority <= LOG_ERR) {
                if (on)
                        *on = ansi_highlight_red();
                if (off)
                        *off = ansi_normal();
                if (highlight)
                        *highlight = ansi_highlight();

        } else if (priority <= LOG_WARNING) {
                if (on)
                        *on = ansi_highlight_yellow();
                if (off)
                        *off = ansi_normal();
                if (highlight)
                        *highlight = ansi_highlight();

        } else if (priority <= LOG_NOTICE) {
                if (on)
                        *on = ansi_highlight();
                if (off)
                        *off = ansi_normal();
                if (highlight)
                        *highlight = ansi_highlight_red();

        } else if (priority >= LOG_DEBUG) {
                if (on)
                        *on = ansi_grey();
                if (off)
                        *off = ansi_normal();
                if (highlight)
                        *highlight = ansi_highlight_red();
        }
}

typedef enum CursorPositionState {
        CURSOR_TEXT,
        CURSOR_ESCAPE,
        CURSOR_ROW,
        CURSOR_COLUMN,
} CursorPositionState;

typedef struct CursorPositionContext {
        CursorPositionState state;
        unsigned row, column;
} CursorPositionContext;

typedef enum BackgroundColorState {
        BACKGROUND_TEXT,
        BACKGROUND_ESCAPE,
        BACKGROUND_BRACKET,
        BACKGROUND_FIRST_ONE,
        BACKGROUND_SECOND_ONE,
        BACKGROUND_SEMICOLON,
        BACKGROUND_R,
        BACKGROUND_G,
        BACKGROUND_B,
        BACKGROUND_RED,
        BACKGROUND_GREEN,
        BACKGROUND_BLUE,
        BACKGROUND_STRING_TERMINATOR,
} BackgroundColorState;

typedef struct BackgroundColorContext {
        BackgroundColorState state;
        uint32_t red, green, blue;
        unsigned red_bits, green_bits, blue_bits;
} BackgroundColorContext;

/* Determine terminal dimensions by means of ANSI sequences: save the cursor via DECSC, position it far
 * to the bottom right (clamped to actual terminal dimensions), read back via DSR where we ended up, and
 * restore cursor via DECRC. Only needs a single DSR round-trip, and always restores the cursor regardless
 * of whether the response is received.
 *
 * Caller must have already opened a non-blocking input fd and configured termios (echo/icanon off). */
/* Common setup for ANSI terminal queries: validate the fds, open a non-blocking input fd, and configure
 * termios with echo and canonical mode disabled. Caller must restore termios and close the fd when done. */
/*
 * See https://terminalguide.namepad.de/seq/csi_st-18/,
 * https://invisible-island.net/xterm/ctlseqs/ctlseqs.html#h3-Functions-using-CSI-_-ordered-by-the-final-character_s_.
 */
#define CSI18_Q  "\x1B[18t"               /* Report the size of the text area in characters */
#define CSI18_Rp "\x1B[8;"                /* Reply prefix */
#define CSI18_R0 CSI18_Rp "1;1t"          /* Shortest reply */
#define CSI18_R1 CSI18_Rp "32766;32766t"  /* Longest reply */

/* Determine terminal dimensions by means of an ANSI CSI 18 sequence.
 *
 * Caller must have already opened a non-blocking input fd and configured termios (echo/icanon off). */
#define MAX_TERMINFO_LENGTH 64
/* python -c 'print("".join(hex(ord(i))[2:] for i in "name").upper())' */
#define DCS_TERMINFO_Q ANSI_DCS "+q" "6E616D65" ANSI_ST
/* The answer is either 0+r… (invalid) or 1+r… (OK). */
#define DCS_TERMINFO_R0 ANSI_DCS "0+r" ANSI_ST
#define DCS_TERMINFO_R1 ANSI_DCS "1+r" "6E616D65" "=" /* This is followed by Pt ST. */
assert_cc(STRLEN(DCS_TERMINFO_R0) <= STRLEN(DCS_TERMINFO_R1 ANSI_ST));

