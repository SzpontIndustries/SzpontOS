/*
 * SzpontOS - UNIX TTY & Line Discipline Driver
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <drivers/tty.h>
#include <drivers/keyboard.h>
#include <drivers/serial.h>
#include <drivers/framebuffer.h>
#include <drivers/speaker.h>
#include <arch/x86_64/io.h>
#include <sched/sched.h>
#include <sched/process.h>
#include <kernel/signal.h>
#include <kernel/kprint.h>
#include <kernel/spinlock.h>
#include <kernel/string.h>

#define TTY_LINE_BUF_SIZE 4096

static termios_t g_tty_termios;
static winsize_t g_tty_winsize;
static spinlock_t g_tty_lock = SPINLOCK_INIT;

/* Canonical line buffer */
static char g_canon_buf[TTY_LINE_BUF_SIZE];
static size_t g_canon_len = 0;
static size_t g_canon_pos = 0;
static bool g_line_ready = false;

static void tty_echo_char(char c) {
    if (c == '\n') {
        fb_console_putc('\r');
        fb_console_putc('\n');
        serial_putc('\r');
        serial_putc('\n');
    } else if (c == '\b') {
        fb_console_putc('\b');
        fb_console_putc(' ');
        fb_console_putc('\b');
        serial_putc('\b');
        serial_putc(' ');
        serial_putc('\b');
    } else if ((uint8_t)c >= 32 || c == '\t') {
        fb_console_putc(c);
        serial_putc(c);
    }
}

static void tty_echo_str(const char *s) {
    while (*s) {
        tty_echo_char(*s++);
    }
}

void tty_init(void) {
    spinlock_init(&g_tty_lock);

    /* Setup standard default POSIX termios flags */
    g_tty_termios.c_iflag = TTY_IFLAG_ICRNL;
    g_tty_termios.c_oflag = TTY_OFLAG_OPOST | TTY_OFLAG_ONLCR;
    g_tty_termios.c_lflag = TTY_LFLAG_ISIG | TTY_LFLAG_ICANON | TTY_LFLAG_ECHO | TTY_LFLAG_ECHOE | TTY_LFLAG_ECHOK;

    memset(g_tty_termios.c_cc, 0, sizeof(g_tty_termios.c_cc));
    g_tty_termios.c_cc[TTY_VINTR] = 0x03;  /* Ctrl+C */
    g_tty_termios.c_cc[TTY_VQUIT] = 0x1C;  /* Ctrl+\ */
    g_tty_termios.c_cc[TTY_VERASE] = 0x7F; /* Backspace / DEL */
    g_tty_termios.c_cc[TTY_VKILL] = 0x15;  /* Ctrl+U */
    g_tty_termios.c_cc[TTY_VEOF] = 0x04;   /* Ctrl+D */
    g_tty_termios.c_cc[TTY_VSUSP] = 0x1A;  /* Ctrl+Z */
    g_tty_termios.c_cc[TTY_VTIME] = 0;
    g_tty_termios.c_cc[TTY_VMIN] = 1;

    g_tty_winsize.ws_row = 25;
    g_tty_winsize.ws_col = 80;
    g_tty_winsize.ws_xpixel = 1280;
    g_tty_winsize.ws_ypixel = 960;

    g_canon_len = 0;
    g_canon_pos = 0;
    g_line_ready = false;

    klog_info("UNIX TTY: Driver initialized with canonical line discipline and signal support");
}

static char tty_get_raw_key(void) {
    while (1) {
        if (keyboard_has_char()) {
            return keyboard_getc();
        }
        if (serial_received()) {
            return serial_getc();
        }
        keyboard_relax();
    }
}

ssize_t tty_read(void *buffer, size_t count) {
    if (!buffer || count == 0)
        return 0;
    char *buf = (char *)buffer;

    /* Raw Mode (ICANON disabled) */
    if (!(g_tty_termios.c_lflag & TTY_LFLAG_ICANON)) {
        size_t nread = 0;
        while (nread < count) {
            char c = tty_get_raw_key();

            /* Check Signals if ISIG enabled */
            if (g_tty_termios.c_lflag & TTY_LFLAG_ISIG) {
                if (c == (char)g_tty_termios.c_cc[TTY_VINTR]) {
                    process_t *fg = process_get_foreground();
                    if (fg)
                        process_send_signal(fg, SIGINT);
                    continue;
                } else if (c == (char)g_tty_termios.c_cc[TTY_VSUSP]) {
                    process_t *fg = process_get_foreground();
                    if (fg)
                        process_send_signal(fg, SIGTSTP);
                    continue;
                } else if (c == (char)g_tty_termios.c_cc[TTY_VQUIT]) {
                    process_t *fg = process_get_foreground();
                    if (fg)
                        process_send_signal(fg, SIGQUIT);
                    continue;
                }
            }

            /* ICRNL / INLCR / IGNCR Input Transformations */
            if (c == '\r') {
                if (g_tty_termios.c_iflag & TTY_IFLAG_IGNCR) {
                    continue;
                }
                if (g_tty_termios.c_iflag & TTY_IFLAG_ICRNL) {
                    c = '\n';
                }
            } else if (c == '\n') {
                if (g_tty_termios.c_iflag & TTY_IFLAG_INLCR) {
                    c = '\r';
                }
            }

            if (g_tty_termios.c_lflag & TTY_LFLAG_ECHO) {
                tty_echo_char(c);
            }

            buf[nread++] = c;
            if (nread >= count || !keyboard_has_char()) {
                break;
            }
        }
        return (ssize_t)nread;
    }

    /* Canonical Mode (ICANON enabled) */
    while (!g_line_ready) {
        char c = tty_get_raw_key();

        /* Signal checks */
        if (g_tty_termios.c_lflag & TTY_LFLAG_ISIG) {
            if (c == (char)g_tty_termios.c_cc[TTY_VINTR]) { /* Ctrl+C */
                process_t *fg = process_get_foreground();
                if (fg)
                    process_send_signal(fg, SIGINT);
                if (g_tty_termios.c_lflag & TTY_LFLAG_ECHO)
                    tty_echo_str("^C\n");
                g_canon_len = 0;
                g_canon_pos = 0;
                continue;
            }
            if (c == (char)g_tty_termios.c_cc[TTY_VSUSP]) { /* Ctrl+Z */
                process_t *fg = process_get_foreground();
                if (fg)
                    process_send_signal(fg, SIGTSTP);
                if (g_tty_termios.c_lflag & TTY_LFLAG_ECHO)
                    tty_echo_str("^Z\n");
                continue;
            }
            if (c == (char)g_tty_termios.c_cc[TTY_VQUIT]) { /* Ctrl+\ */
                process_t *fg = process_get_foreground();
                if (fg)
                    process_send_signal(fg, SIGQUIT);
                if (g_tty_termios.c_lflag & TTY_LFLAG_ECHO)
                    tty_echo_str("^\\\n");
                continue;
            }
        }

        /* EOF (Ctrl+D) */
        if (c == (char)g_tty_termios.c_cc[TTY_VEOF]) {
            if (g_canon_len == 0) {
                return 0; /* EOF */
            }
            g_line_ready = true;
            break;
        }

        /* Enter / Return */
        if (c == '\r' || c == '\n') {
            if (g_tty_termios.c_iflag & TTY_IFLAG_ICRNL)
                c = '\n';
            if (g_canon_len < TTY_LINE_BUF_SIZE - 1) {
                g_canon_buf[g_canon_len++] = '\n';
            }
            if (g_tty_termios.c_lflag & TTY_LFLAG_ECHO) {
                tty_echo_char('\n');
            }
            g_line_ready = true;
            break;
        }

        /* Backspace / Erase */
        if (c == '\b' || c == 0x7F || c == (char)g_tty_termios.c_cc[TTY_VERASE]) {
            if (g_canon_len > 0) {
                g_canon_len--;
                if (g_tty_termios.c_lflag & TTY_LFLAG_ECHO) {
                    tty_echo_char('\b');
                }
            }
            continue;
        }

        /* Kill Line (Ctrl+U) */
        if (c == (char)g_tty_termios.c_cc[TTY_VKILL]) {
            while (g_canon_len > 0) {
                g_canon_len--;
                if (g_tty_termios.c_lflag & TTY_LFLAG_ECHO) {
                    tty_echo_char('\b');
                }
            }
            continue;
        }

        /* Word Erase (Ctrl+W) */
        if (c == 0x17) {
            while (g_canon_len > 0 && g_canon_buf[g_canon_len - 1] == ' ') {
                g_canon_len--;
                if (g_tty_termios.c_lflag & TTY_LFLAG_ECHO)
                    tty_echo_char('\b');
            }
            while (g_canon_len > 0 && g_canon_buf[g_canon_len - 1] != ' ') {
                g_canon_len--;
                if (g_tty_termios.c_lflag & TTY_LFLAG_ECHO)
                    tty_echo_char('\b');
            }
            continue;
        }

        /* Regular printable character */
        if (g_canon_len < TTY_LINE_BUF_SIZE - 1) {
            g_canon_buf[g_canon_len++] = c;
            if (g_tty_termios.c_lflag & TTY_LFLAG_ECHO) {
                tty_echo_char(c);
            }
        }
    }

    /* Copy from canonical line buffer to user buffer */
    size_t remaining = g_canon_len - g_canon_pos;
    size_t to_copy = count < remaining ? count : remaining;

    memcpy(buf, g_canon_buf + g_canon_pos, to_copy);
    g_canon_pos += to_copy;

    if (g_canon_pos >= g_canon_len) {
        g_canon_len = 0;
        g_canon_pos = 0;
        g_line_ready = false;
    }

    return (ssize_t)to_copy;
}

ssize_t tty_write(const void *buffer, size_t count) {
    if (!buffer || count == 0)
        return 0;
    const char *str = (const char *)buffer;

    for (size_t i = 0; i < count; i++) {
        char c = str[i];
        if (c == '\a') {
            speaker_beep(800, 50); /* Terminal Bell */
            continue;
        }

        if (c == '\n' && (g_tty_termios.c_oflag & TTY_OFLAG_ONLCR)) {
            fb_console_putc('\r');
            fb_console_putc('\n');
            serial_putc('\r');
            serial_putc('\n');
        } else {
            fb_console_putc(c);
            serial_putc(c);
        }
    }

    return (ssize_t)count;
}

int tty_ioctl(uint64_t request, void *arg) {
    switch (request) {
    case 0x5401: /* TCGETS */
        if (!arg)
            return -1;
        memcpy(arg, &g_tty_termios, sizeof(termios_t));
        return 0;

    case 0x5402: /* TCSETS */
    case 0x5403: /* TCSETSW */
    case 0x5404: /* TCSETSF */
        if (!arg)
            return -1;
        memcpy(&g_tty_termios, arg, sizeof(termios_t));
        return 0;

    case 0x5413: { /* TIOCGWINSZ */
        if (!arg)
            return -1;
        size_t cols = fb_get_cols();
        size_t rows = fb_get_rows();
        g_tty_winsize.ws_col = (cols > 0) ? (uint16_t)cols : 80;
        g_tty_winsize.ws_row = (rows > 0) ? (uint16_t)rows : 24;
        g_tty_winsize.ws_xpixel = (uint16_t)fb_get_width();
        g_tty_winsize.ws_ypixel = (uint16_t)fb_get_height();
        memcpy(arg, &g_tty_winsize, sizeof(winsize_t));
        return 0;
    }

    case 0x5414: /* TIOCSWINSZ */
        if (!arg)
            return -1;
        memcpy(&g_tty_winsize, arg, sizeof(winsize_t));
        return 0;

    case 0x540F: /* TIOCGPGRP */
        if (!arg)
            return -1;
        *(int *)arg = 1;
        return 0;

    case 0x5410: /* TIOCSPGRP */
        return 0;

    case 0x540E: /* TIOCSCTTY */ {
        process_t *curr = sched_get_current_process();
        if (curr) {
            curr->has_ctty = true;
            if (!curr->ctty) {
                curr->ctty = vfs_lookup("/dev/console");
            }
        }
        return 0;
    }

    case 0x5422: /* TIOCNOTTY */ {
        process_t *curr = sched_get_current_process();
        if (curr) {
            curr->has_ctty = false;
            curr->ctty = NULL;
        }
        return 0;
    }

    case 0x541B: { /* FIONREAD */
        if (!arg)
            return -1;
        int *avail = (int *)arg;
        *avail = (keyboard_has_char() || serial_received()) ? 1 : 0;
        return 0;
    }

    case 0x4B3A: { /* KDSETMODE (0 = KD_TEXT, 1 = KD_GRAPHICS) */
        uintptr_t mode = (uintptr_t)arg;
        if (mode == 1) {
            fb_set_graphics_mode(true);
        } else {
            fb_set_graphics_mode(false);
        }
        return 0;
    }

    case 0x4B3B: { /* KDGETMODE */
        if (!arg)
            return -1;
        *(int *)arg = fb_is_graphics_mode() ? 1 : 0;
        return 0;
    }

    case 0x80045430: /* TIOCGPTN */
        return -25; /* -ENOTTY: Console /dev/tty is not a pseudo-terminal */

    default:
        return 0; /* Graceful fallback */
    }
}

bool tty_has_input(void) {
    if (g_canon_len > g_canon_pos || g_line_ready)
        return true;
    return (keyboard_has_char() || serial_received());
}

