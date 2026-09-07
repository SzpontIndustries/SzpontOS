/*
 * SzpontOS - Freestanding Terminal Curses Implementation (libcurses)
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <curses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <term.h>

WINDOW *stdscr = NULL;
WINDOW *curscr = NULL;
int LINES = 24;
int COLS = 80;
int TABSIZE = 8;

static bool g_curses_initialized = false;
static struct termios g_orig_termios;
static bool g_raw_mode = false;
static int g_unget_ch = -1;

struct color_pair_info {
    short fg;
    short bg;
};
static struct color_pair_info g_pairs[COLOR_PAIRS];

static void term_send(const char *str) {
    if (str && *str) {
        write(STDOUT_FILENO, str, strlen(str));
    }
}

static void term_send_fmt(const char *fmt, ...) {
    char buf[128];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len > 0) {
        write(STDOUT_FILENO, buf, (size_t)len);
    }
}

WINDOW *newwin(int nlines, int ncols, int begy, int begx) {
    if (nlines <= 0)
        nlines = LINES - begy;
    if (ncols <= 0)
        ncols = COLS - begx;

    WINDOW *win = (WINDOW *)malloc(sizeof(WINDOW));
    if (!win)
        return NULL;

    win->_maxy = nlines;
    win->_maxx = ncols;
    win->_begy = begy;
    win->_begx = begx;
    win->_cury = 0;
    win->_curx = 0;
    win->_flags = 0;
    win->_scroll = true;
    win->_keypad = true;
    win->_delay = -1;
    win->_attrs_current = A_NORMAL;
    win->_bkgd = ' ';
    win->_leaveok = false;
    win->_nodelay = false;
    win->_regtop = 0;
    win->_regbottom = nlines - 1;

    win->_lines = (chtype **)malloc(sizeof(chtype *) * nlines);
    win->_attrs = (chtype **)malloc(sizeof(chtype *) * nlines);

    for (int y = 0; y < nlines; y++) {
        win->_lines[y] = (chtype *)malloc(sizeof(chtype) * ncols);
        win->_attrs[y] = (chtype *)malloc(sizeof(chtype) * ncols);
        for (int x = 0; x < ncols; x++) {
            win->_lines[y][x] = ' ';
            win->_attrs[y][x] = A_NORMAL;
        }
    }

    return win;
}

int delwin(WINDOW *win) {
    if (!win)
        return ERR;
    if (win->_lines) {
        for (int y = 0; y < win->_maxy; y++) {
            if (win->_lines[y])
                free(win->_lines[y]);
            if (win->_attrs[y])
                free(win->_attrs[y]);
        }
        free(win->_lines);
        free(win->_attrs);
    }
    free(win);
    return OK;
}

WINDOW *subwin(WINDOW *orig, int nlines, int ncols, int begy, int begx) {
    return newwin(nlines, ncols, begy, begx);
}

WINDOW *derwin(WINDOW *orig, int nlines, int ncols, int begy, int begx) {
    if (!orig)
        return NULL;
    return newwin(nlines, ncols, orig->_begy + begy, orig->_begx + begx);
}

int mvwin(WINDOW *win, int y, int x) {
    if (!win)
        return ERR;
    win->_begy = y;
    win->_begx = x;
    return OK;
}

WINDOW *initscr(void) {
    if (g_curses_initialized)
        return stdscr;

    tcgetattr(STDIN_FILENO, &g_orig_termios);

    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
        LINES = ws.ws_row;
        COLS = ws.ws_col;
    } else {
        LINES = 24;
        COLS = 80;
    }

    stdscr = newwin(LINES, COLS, 0, 0);
    curscr = newwin(LINES, COLS, 0, 0);

    /* Initialize color pairs to default */
    for (int i = 0; i < COLOR_PAIRS; i++) {
        g_pairs[i].fg = COLOR_WHITE;
        g_pairs[i].bg = COLOR_BLACK;
    }

    /* Switch to alternate screen buffer, clear screen, hide cursor initially */
    term_send("\033[?1049h\033[2J\033[H");

    cbreak();
    noecho();

    g_curses_initialized = true;
    return stdscr;
}

int endwin(void) {
    if (!g_curses_initialized)
        return ERR;

    /* Restore original terminal modes */
    tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
    g_raw_mode = false;

    /* Restore normal screen buffer, show cursor */
    term_send("\033[?25h\033[0m\033[?1049l");

    g_curses_initialized = false;
    return OK;
}

bool isendwin(void) {
    return !g_curses_initialized;
}

int cbreak(void) {
    struct termios t;
    tcgetattr(STDIN_FILENO, &t);
    t.c_lflag &= ~(ICANON);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
    g_raw_mode = true;
    return OK;
}

int nocbreak(void) {
    struct termios t;
    tcgetattr(STDIN_FILENO, &t);
    t.c_lflag |= ICANON;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
    return OK;
}

int raw(void) {
    struct termios t;
    tcgetattr(STDIN_FILENO, &t);
    t.c_lflag &= ~(ICANON | ISIG | ECHO);
    t.c_iflag &= ~(IXON | ICRNL);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
    g_raw_mode = true;
    return OK;
}

int noraw(void) {
    return nocbreak();
}

int echo(void) {
    struct termios t;
    tcgetattr(STDIN_FILENO, &t);
    t.c_lflag |= ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
    return OK;
}

int noecho(void) {
    struct termios t;
    tcgetattr(STDIN_FILENO, &t);
    t.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
    return OK;
}

int nl(void) {
    return OK;
}

int nonl(void) {
    return OK;
}

int keypad(WINDOW *win, bool bf) {
    if (!win)
        return ERR;
    win->_keypad = bf;
    return OK;
}

int nodelay(WINDOW *win, bool bf) {
    if (!win)
        return ERR;
    win->_nodelay = bf;
    return OK;
}

int timeout(int delay) {
    return wtimeout(stdscr, delay);
}

int wtimeout(WINDOW *win, int delay) {
    if (!win)
        return ERR;
    win->_delay = delay;
    return OK;
}

int halfdelay(int tenths) {
    (void)tenths;
    return OK;
}

int intrflush(WINDOW *win, bool bf) {
    (void)win;
    (void)bf;
    return OK;
}
int qiflush(void) {
    return OK;
}
int flushinp(void) {
    return OK;
}
int typeahead(int fd) {
    (void)fd;
    return OK;
}
int leaveok(WINDOW *win, bool bf) {
    if (win)
        win->_leaveok = bf;
    return OK;
}
int scrollok(WINDOW *win, bool bf) {
    if (win)
        win->_scroll = bf;
    return OK;
}
int wsetscrreg(WINDOW *win, int top, int bot) {
    if (!win || top < 0 || bot < top || bot >= win->_maxy)
        return ERR;
    win->_regtop = top;
    win->_regbottom = bot;
    return OK;
}
int setscrreg(int top, int bot) {
    return wsetscrreg(stdscr, top, bot);
}
int idlok(WINDOW *win, bool bf) {
    (void)win;
    (void)bf;
    return OK;
}
void idcok(WINDOW *win, bool bf) {
    (void)win;
    (void)bf;
}

int curs_set(int visibility) {
    if (visibility == 0) {
        term_send("\033[?25l"); /* Hide */
    } else {
        term_send("\033[?25h"); /* Show */
    }
    return OK;
}

int wmove(WINDOW *win, int y, int x) {
    if (!win)
        return ERR;
    if (y < 0 || y >= win->_maxy || x < 0 || x >= win->_maxx)
        return ERR;
    win->_cury = y;
    win->_curx = x;
    return OK;
}

int move(int y, int x) {
    return wmove(stdscr, y, x);
}

static void win_scroll_up(WINDOW *win) {
    if (!win || win->_maxy <= 1)
        return;
    chtype *top_line = win->_lines[0];
    chtype *top_attr = win->_attrs[0];
    for (int y = 0; y + 1 < win->_maxy; y++) {
        win->_lines[y] = win->_lines[y + 1];
        win->_attrs[y] = win->_attrs[y + 1];
    }
    win->_lines[win->_maxy - 1] = top_line;
    win->_attrs[win->_maxy - 1] = top_attr;
    for (int x = 0; x < win->_maxx; x++) {
        win->_lines[win->_maxy - 1][x] = ' ';
        win->_attrs[win->_maxy - 1][x] = A_NORMAL;
    }
}

int waddch(WINDOW *win, const chtype ch) {
    if (!win)
        return ERR;

    char c = (char)(ch & A_CHARTEXT);
    attr_t attr = (ch & ~A_CHARTEXT) | win->_attrs_current;

    if (c == '\n') {
        win->_curx = 0;
        win->_cury++;
        if (win->_cury >= win->_maxy) {
            if (win->_scroll) {
                win_scroll_up(win);
                win->_cury = win->_maxy - 1;
            } else {
                win->_cury = win->_maxy - 1;
            }
        }
        return OK;
    } else if (c == '\r') {
        win->_curx = 0;
        return OK;
    } else if (c == '\t') {
        int next_tab = (win->_curx + TABSIZE) & ~(TABSIZE - 1);
        while (win->_curx < next_tab && win->_curx < win->_maxx) {
            win->_lines[win->_cury][win->_curx] = ' ';
            win->_attrs[win->_cury][win->_curx] = attr;
            win->_curx++;
        }
        if (win->_curx >= win->_maxx) {
            win->_curx = 0;
            win->_cury++;
            if (win->_cury >= win->_maxy && win->_scroll) {
                win_scroll_up(win);
                win->_cury = win->_maxy - 1;
            }
        }
        return OK;
    } else if (c == '\b') {
        if (win->_curx > 0)
            win->_curx--;
        return OK;
    }

    if (win->_cury >= 0 && win->_cury < win->_maxy && win->_curx >= 0 && win->_curx < win->_maxx) {
        win->_lines[win->_cury][win->_curx] = ch & A_CHARTEXT;
        win->_attrs[win->_cury][win->_curx] = attr;
        win->_curx++;
        if (win->_curx >= win->_maxx) {
            win->_curx = 0;
            win->_cury++;
            if (win->_cury >= win->_maxy) {
                if (win->_scroll) {
                    win_scroll_up(win);
                    win->_cury = win->_maxy - 1;
                } else {
                    win->_cury = win->_maxy - 1;
                }
            }
        }
    }

    return OK;
}

int addch(const chtype ch) {
    return waddch(stdscr, ch);
}

int mvwaddch(WINDOW *win, int y, int x, const chtype ch) {
    if (wmove(win, y, x) == ERR)
        return ERR;
    return waddch(win, ch);
}

int waddnstr(WINDOW *win, const char *str, int n) {
    if (!win || !str)
        return ERR;
    int count = 0;
    while (*str && (n < 0 || count < n)) {
        waddch(win, (unsigned char)*str++);
        count++;
    }
    return OK;
}

int waddstr(WINDOW *win, const char *str) {
    return waddnstr(win, str, -1);
}

int addstr(const char *str) {
    return waddstr(stdscr, str);
}

int addnstr(const char *str, int n) {
    return waddnstr(stdscr, str, n);
}

int mvwaddstr(WINDOW *win, int y, int x, const char *str) {
    if (wmove(win, y, x) == ERR)
        return ERR;
    return waddstr(win, str);
}

int mvwaddnstr(WINDOW *win, int y, int x, const char *str, int n) {
    if (wmove(win, y, x) == ERR)
        return ERR;
    return waddnstr(win, str, n);
}

int vw_printw(WINDOW *win, const char *fmt, va_list varglist) {
    char buf[1024];
    int len = vsnprintf(buf, sizeof(buf), fmt, varglist);
    if (len > 0) {
        waddnstr(win, buf, len);
    }
    return len >= 0 ? OK : ERR;
}

int vwprintw(WINDOW *win, const char *fmt, va_list varglist) {
    return vw_printw(win, fmt, varglist);
}

int wprintw(WINDOW *win, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vw_printw(win, fmt, args);
    va_end(args);
    return ret;
}

int printw(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vw_printw(stdscr, fmt, args);
    va_end(args);
    return ret;
}

int mvwprintw(WINDOW *win, int y, int x, const char *fmt, ...) {
    if (wmove(win, y, x) == ERR)
        return ERR;
    va_list args;
    va_start(args, fmt);
    int ret = vw_printw(win, fmt, args);
    va_end(args);
    return ret;
}

int werase(WINDOW *win) {
    if (!win)
        return ERR;
    for (int y = 0; y < win->_maxy; y++) {
        for (int x = 0; x < win->_maxx; x++) {
            win->_lines[y][x] = ' ';
            win->_attrs[y][x] = A_NORMAL;
        }
    }
    win->_cury = 0;
    win->_curx = 0;
    return OK;
}

int erase(void) {
    return werase(stdscr);
}

int wclear(WINDOW *win) {
    if (!win)
        return ERR;
    werase(win);
    return OK;
}

int clear(void) {
    return wclear(stdscr);
}

int wclrtoeol(WINDOW *win) {
    if (!win)
        return ERR;
    int y = win->_cury;
    for (int x = win->_curx; x < win->_maxx; x++) {
        win->_lines[y][x] = ' ';
        win->_attrs[y][x] = win->_attrs_current;
    }
    return OK;
}

int clrtoeol(void) {
    return wclrtoeol(stdscr);
}

int wclrtobot(WINDOW *win) {
    if (!win)
        return ERR;
    wclrtoeol(win);
    for (int y = win->_cury + 1; y < win->_maxy; y++) {
        for (int x = 0; x < win->_maxx; x++) {
            win->_lines[y][x] = ' ';
            win->_attrs[y][x] = win->_attrs_current;
        }
    }
    return OK;
}

int clrtobot(void) {
    return wclrtobot(stdscr);
}

int wscrl(WINDOW *win, int n) {
    if (!win || n == 0)
        return OK;
    int top = win->_regtop;
    int bot = win->_regbottom;
    if (top >= bot || bot >= win->_maxy) {
        top = 0;
        bot = win->_maxy - 1;
    }

    if (n > 0) {
        for (int step = 0; step < n; step++) {
            for (int y = top; y < bot; y++) {
                memcpy(win->_lines[y], win->_lines[y + 1], win->_maxx);
                memcpy(win->_attrs[y], win->_attrs[y + 1], win->_maxx * sizeof(attr_t));
            }
            memset(win->_lines[bot], ' ', win->_maxx);
            for (int x = 0; x < win->_maxx; x++) {
                win->_attrs[bot][x] = win->_bkgd;
            }
        }
    } else {
        int lines = -n;
        for (int step = 0; step < lines; step++) {
            for (int y = bot; y > top; y--) {
                memcpy(win->_lines[y], win->_lines[y - 1], win->_maxx);
                memcpy(win->_attrs[y], win->_attrs[y - 1], win->_maxx * sizeof(attr_t));
            }
            memset(win->_lines[top], ' ', win->_maxx);
            for (int x = 0; x < win->_maxx; x++) {
                win->_attrs[top][x] = win->_bkgd;
            }
        }
    }
    return OK;
}

int scrl(int n) {
    return wscrl(stdscr, n);
}

int scroll(WINDOW *win) {
    return wscrl(win, 1);
}

static void apply_attributes(attr_t attr) {
    term_send("\033[0m"); /* Reset */

    if (attr & A_BOLD)
        term_send("\033[1m");
    if (attr & A_DIM)
        term_send("\033[2m");
    if (attr & A_UNDERLINE)
        term_send("\033[4m");
    if (attr & A_BLINK)
        term_send("\033[5m");
    if (attr & A_REVERSE)
        term_send("\033[7m");

    int pair = PAIR_NUMBER(attr);
    if (pair > 0 && pair < COLOR_PAIRS) {
        short fg = g_pairs[pair].fg;
        short bg = g_pairs[pair].bg;
        if (fg >= 0 && fg < 8)
            term_send_fmt("\033[%dm", 30 + fg);
        if (bg >= 0 && bg < 8)
            term_send_fmt("\033[%dm", 40 + bg);
    }
}

int wrefresh(WINDOW *win) {
    if (!win)
        return ERR;

    /* Render window content to terminal */
    attr_t current_term_attr = (attr_t)-1;

    for (int y = 0; y < win->_maxy; y++) {
        int screen_y = win->_begy + y + 1; /* 1-based */
        int screen_x = win->_begx + 1;     /* 1-based */

        term_send_fmt("\033[%d;%dH", screen_y, screen_x);

        for (int x = 0; x < win->_maxx; x++) {
            attr_t a = win->_attrs[y][x];
            if (a != current_term_attr) {
                apply_attributes(a);
                current_term_attr = a;
            }
            char c = (char)(win->_lines[y][x] ? win->_lines[y][x] : ' ');
            write(STDOUT_FILENO, &c, 1);
        }
    }

    term_send("\033[0m");

    /* Move hardware cursor to current position */
    if (!win->_leaveok) {
        term_send_fmt("\033[%d;%dH", win->_begy + win->_cury + 1, win->_begx + win->_curx + 1);
    }

    return OK;
}

int refresh(void) {
    return wrefresh(stdscr);
}

int wnoutrefresh(WINDOW *win) {
    (void)win;
    return OK;
}

int doupdate(void) {
    return wrefresh(stdscr);
}

int redrawwin(WINDOW *win) {
    return wrefresh(win);
}

int wredrawln(WINDOW *win, int beg_line, int num_lines) {
    (void)beg_line;
    (void)num_lines;
    return wrefresh(win);
}

int ungetch(int ch) {
    g_unget_ch = ch;
    return OK;
}

int wgetch(WINDOW *win) {
    if (g_unget_ch != -1) {
        int ch = g_unget_ch;
        g_unget_ch = -1;
        return ch;
    }

    unsigned char c = 0;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n <= 0)
        return ERR;

    if (!win || !win->_keypad) {
        return c;
    }

    /* Escape sequence handling */
    if (c == 0x1B) {
        /* Check if more characters follow */
        int avail = 0;
        ioctl(STDIN_FILENO, FIONREAD, &avail);
        if (avail <= 0) {
            return 0x1B; /* Plain ESC */
        }

        unsigned char seq[8] = {0};
        ssize_t sn = read(STDIN_FILENO, seq, 1);
        if (sn <= 0)
            return 0x1B;

        if (seq[0] == '[' || seq[0] == 'O') {
            unsigned char code = 0;
            if (read(STDIN_FILENO, &code, 1) <= 0)
                return 0x1B;

            if (seq[0] == '[') {
                switch (code) {
                case 'A':
                    return KEY_UP;
                case 'B':
                    return KEY_DOWN;
                case 'C':
                    return KEY_RIGHT;
                case 'D':
                    return KEY_LEFT;
                case 'H':
                    return KEY_HOME;
                case 'F':
                    return KEY_END;
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6': {
                    unsigned char tilde = 0;
                    read(STDIN_FILENO, &tilde, 1);
                    if (code == '1')
                        return KEY_HOME;
                    if (code == '2')
                        return KEY_IC;
                    if (code == '3')
                        return KEY_DC;
                    if (code == '4')
                        return KEY_END;
                    if (code == '5')
                        return KEY_PPAGE;
                    if (code == '6')
                        return KEY_NPAGE;
                    break;
                }
                }
            } else if (seq[0] == 'O') {
                switch (code) {
                case 'H':
                    return KEY_HOME;
                case 'F':
                    return KEY_END;
                case 'P':
                    return KEY_F(1);
                case 'Q':
                    return KEY_F(2);
                case 'R':
                    return KEY_F(3);
                case 'S':
                    return KEY_F(4);
                }
            }
        }
        return 0x1B;
    }

    if (c == 0x7F || c == 0x08) {
        return KEY_BACKSPACE;
    }

    return c;
}

int getch(void) {
    return wgetch(stdscr);
}

int wgetnstr(WINDOW *win, char *str, int n) {
    if (!win || !str || n <= 0)
        return ERR;
    int pos = 0;
    while (pos + 1 < n) {
        int ch = wgetch(win);
        if (ch == '\n' || ch == '\r' || ch == ERR)
            break;
        if (ch == KEY_BACKSPACE) {
            if (pos > 0) {
                pos--;
                waddch(win, '\b');
                waddch(win, ' ');
                waddch(win, '\b');
            }
            continue;
        }
        str[pos++] = (char)ch;
        waddch(win, ch);
        wrefresh(win);
    }
    str[pos] = '\0';
    return OK;
}

int getnstr(char *str, int n) {
    return wgetnstr(stdscr, str, n);
}

int wattron(WINDOW *win, int attrs) {
    if (!win)
        return ERR;
    win->_attrs_current |= attrs;
    return OK;
}

int wattroff(WINDOW *win, int attrs) {
    if (!win)
        return ERR;
    win->_attrs_current &= ~attrs;
    return OK;
}

int wattrset(WINDOW *win, int attrs) {
    if (!win)
        return ERR;
    win->_attrs_current = attrs;
    return OK;
}

int attron(int attrs) {
    return wattron(stdscr, attrs);
}
int attroff(int attrs) {
    return wattroff(stdscr, attrs);
}
int attrset(int attrs) {
    return wattrset(stdscr, attrs);
}

int wstandout(WINDOW *win) {
    return wattron(win, A_STANDOUT);
}
int wstandend(WINDOW *win) {
    return wattroff(win, A_STANDOUT);
}
int standout(void) {
    return wstandout(stdscr);
}
int standend(void) {
    return wstandend(stdscr);
}

int wbkgd(WINDOW *win, chtype ch) {
    if (!win)
        return ERR;
    win->_bkgd = ch;
    return OK;
}

void wbkgdset(WINDOW *win, chtype ch) {
    if (win)
        win->_bkgd = ch;
}

int wchgat(WINDOW *win, int n, attr_t attr, short color, const void *opts) {
    (void)opts;
    if (!win)
        return ERR;
    int y = win->_cury;
    int count = (n < 0 || win->_curx + n > win->_maxx) ? (win->_maxx - win->_curx) : n;
    attr_t full_attr = attr | COLOR_PAIR(color);
    for (int x = win->_curx; x < win->_curx + count; x++) {
        win->_attrs[y][x] = full_attr;
    }
    return OK;
}

int mvwchgat(WINDOW *win, int y, int x, int n, attr_t attr, short color, const void *opts) {
    if (wmove(win, y, x) == ERR)
        return ERR;
    return wchgat(win, n, attr, color, opts);
}

int start_color(void) {
    return OK;
}

bool has_colors(void) {
    return true;
}

bool can_change_color(void) {
    return false;
}

int init_pair(short pair, short f, short b) {
    if (pair <= 0 || pair >= COLOR_PAIRS)
        return ERR;
    g_pairs[pair].fg = f;
    g_pairs[pair].bg = b;
    return OK;
}

int pair_content(short pair, short *f, short *b) {
    if (pair <= 0 || pair >= COLOR_PAIRS)
        return ERR;
    if (f)
        *f = g_pairs[pair].fg;
    if (b)
        *b = g_pairs[pair].bg;
    return OK;
}

int init_color(short color, short r, short g, short b) {
    (void)color;
    (void)r;
    (void)g;
    (void)b;
    return OK;
}

int color_content(short color, short *r, short *g, short *b) {
    (void)color;
    if (r)
        *r = 1000;
    if (g)
        *g = 1000;
    if (b)
        *b = 1000;
    return OK;
}

int use_default_colors(void) {
    return OK;
}

int beep(void) {
    term_send("\a");
    return OK;
}

int flash(void) {
    return beep();
}

int napms(int ms) {
    if (ms <= 0)
        return OK;
    unsigned int sec = ms / 1000;
    if (sec > 0)
        sleep(sec);
    return OK;
}

char *termname(void) {
    return "xterm-256color";
}

int erasechar(void) {
    return '\b';
}
int killchar(void) {
    return 0x15;
}

static TERMINAL g_cur_term;
TERMINAL *cur_term = &g_cur_term;

char PC = '\0';
char *UP = NULL;
char *BC = NULL;
short ospeed = 0;

int setupterm(const char *term, int fildes, int *errret) {
    (void)term;
    (void)fildes;
    if (errret)
        *errret = 1;
    return OK;
}

int set_curterm(TERMINAL *nterm) {
    if (nterm)
        cur_term = nterm;
    return OK;
}

int del_curterm(TERMINAL *oterm) {
    (void)oterm;
    return OK;
}

int restartterm(const char *term, int fildes, int *errret) {
    return setupterm(term, fildes, errret);
}

char *tparm(const char *str, ...) {
    return (char *)str;
}

int tputs(const char *str, int affcnt, int (*putc_fn)(int)) {
    (void)affcnt;
    if (!str)
        return ERR;
    while (*str) {
        if (putc_fn)
            putc_fn((unsigned char)*str);
        else {
            char c = *str;
            write(STDOUT_FILENO, &c, 1);
        }
        str++;
    }
    return OK;
}

int putp(const char *str) {
    return tputs(str, 1, NULL);
}

int tigetflag(const char *capname) {
    (void)capname;
    return 0;
}

int tigetnum(const char *capname) {
    if (capname && strcmp(capname, "cols") == 0)
        return COLS;
    if (capname && strcmp(capname, "lines") == 0)
        return LINES;
    if (capname && strcmp(capname, "colors") == 0)
        return 8;
    return -1;
}

char *tigetstr(const char *capname) {
    (void)capname;
    return NULL;
}

int tgetent(char *bp, const char *name) {
    (void)bp;
    (void)name;
    return 1;
}

int tgetflag(const char *id) {
    (void)id;
    return 0;
}

int tgetnum(const char *id) {
    if (id && strcmp(id, "co") == 0)
        return COLS;
    if (id && strcmp(id, "li") == 0)
        return LINES;
    return -1;
}

char *tgetstr(const char *id, char **area) {
    (void)id;
    (void)area;
    return NULL;
}

char *tgoto(const char *cap, int col, int row) {
    static char buf[64];
    snprintf(buf, sizeof(buf), "\033[%d;%dH", row + 1, col + 1);
    (void)cap;
    return buf;
}

const char *unctrl(chtype ch) {
    static char buf[8];
    unsigned char c = (unsigned char)(ch & 0xFF);
    if (c < 32) {
        buf[0] = '^';
        buf[1] = (char)('@' + c);
        buf[2] = '\0';
    } else if (c == 127) {
        buf[0] = '^';
        buf[1] = '?';
        buf[2] = '\0';
    } else {
        buf[0] = (char)c;
        buf[1] = '\0';
    }
    return buf;
}
