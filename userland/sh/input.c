/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kenneth Almquist.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdio.h>	/* defines BUFSIZ */
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <dirent.h>
#include <sys/stat.h>
#include <poll.h>

/*
 * This file implements the input routines used by the parser.
 */

#include "shell.h"
#include "redir.h"
#include "syntax.h"
#include "input.h"
#include "output.h"
#include "options.h"
#include "memalloc.h"
#include "error.h"
#include "alias.h"
#include "parser.h"
#ifndef NO_HISTORY
#include "myhistedit.h"
#endif
#include "trap.h"

#define EOF_NLEFT -99		/* value of parsenleft when EOF pushed back */

struct strpush {
	struct strpush *prev;	/* preceding string on stack */
	const char *prevstring;
	int prevnleft;
	int prevlleft;
	struct alias *ap;	/* if push was associated with an alias */
};

/*
 * The parsefile structure pointed to by the global variable parsefile
 * contains information about the current file being read.
 */

struct parsefile {
	struct parsefile *prev;	/* preceding file on stack */
	int linno;		/* current line */
	int fd;			/* file descriptor (or -1 if string) */
	int nleft;		/* number of chars left in this line */
	int lleft;		/* number of lines left in this buffer */
	const char *nextc;	/* next char in buffer */
	char *buf;		/* input buffer */
	size_t bufsize;		/* input buffer size */
	struct strpush *strpush; /* for pushing strings at this level */
	struct strpush basestrpush; /* so pushing one is fast */
};


int plinno = 1;			/* input line number */
int parsenleft;			/* copy of parsefile->nleft */
static int parselleft;		/* copy of parsefile->lleft */
const char *parsenextc;		/* copy of parsefile->nextc */
static char basebuf[BUFSIZ + 1];/* buffer for top level input file */
static struct parsefile basepf = {	/* top level input file */
	.nextc = basebuf,
	.buf = basebuf,
	.bufsize = sizeof(basebuf),
};
static struct parsefile *parsefile = &basepf;	/* current input file */
int whichprompt;		/* 1 == PS1, 2 == PS2 */

static void pushfile(void);
static int preadfd(void);
static void popstring(void);

void
resetinput(void)
{
	popallfiles();
	parselleft = parsenleft = 0;	/* clear input buffer */
}



/*
 * Read a character from the script, returning PEOF on end of file.
 * Nul characters in the input are silently discarded.
 */

int
pgetc(void)
{
	return pgetc_macro();
}


#define SH_HIST_MAX 64
#define SH_LINE_MAX 1024

static char s_history[SH_HIST_MAX][SH_LINE_MAX];
static int s_history_count = 0;

static void
sh_history_add(const char *cmd)
{
	if (!cmd || !*cmd)
		return;
	if (s_history_count > 0 &&
	    strcmp(s_history[(s_history_count - 1) % SH_HIST_MAX], cmd) == 0)
		return;
	strncpy(s_history[s_history_count % SH_HIST_MAX], cmd, SH_LINE_MAX - 1);
	s_history[s_history_count % SH_HIST_MAX][SH_LINE_MAX - 1] = '\0';
	s_history_count++;
}

static inline void
term_write(const void *buf, size_t n)
{
	(void)write(2, buf, n);
}

static inline void
term_putc(char c)
{
	(void)write(2, &c, 1);
}

static void
sh_tab_complete(char *line, int *len, int *cursor)
{
	int cur = *cursor;
	int start = cur - 1;
	while (start >= 0 && line[start] != ' ' && line[start] != '\t' &&
	       line[start] != '|' && line[start] != ';' && line[start] != '&') {
		start--;
	}
	start++;

	int word_len = cur - start;
	char word[256];
	if (word_len <= 0 || word_len >= (int)sizeof(word))
		return;
	memcpy(word, &line[start], word_len);
	word[word_len] = '\0';

	/* Determine if this is a command (first token of line or pipeline) */
	int is_cmd = 1;
	for (int i = start - 1; i >= 0; i--) {
		if (line[i] == ' ' || line[i] == '\t')
			continue;
		if (line[i] == '|' || line[i] == ';' || line[i] == '&')
			break;
		is_cmd = 0;
		break;
	}

	char dir_path[256] = ".";
	const char *file_prefix = word;
	char *slash = strrchr(word, '/');
	if (slash != NULL) {
		int dir_len = slash - word;
		if (dir_len == 0) {
			strcpy(dir_path, "/");
		} else {
			memcpy(dir_path, word, dir_len);
			dir_path[dir_len] = '\0';
		}
		file_prefix = slash + 1;
	} else if (is_cmd) {
		strcpy(dir_path, "/bin");
	}

	DIR *dp = opendir(dir_path);
	if (!dp && is_cmd && slash == NULL) {
		strcpy(dir_path, ".");
		dp = opendir(dir_path);
	}
	if (!dp)
		return;

	char matches[32][64];
	int is_dir[32];
	int match_count = 0;
	size_t prefix_len = strlen(file_prefix);

	struct dirent *de;
	while ((de = readdir(dp)) != NULL && match_count < 32) {
		if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
			continue;
		if (strncmp(de->d_name, file_prefix, prefix_len) == 0) {
			strncpy(matches[match_count], de->d_name, sizeof(matches[0]) - 1);
			matches[match_count][sizeof(matches[0]) - 1] = '\0';

			char full[512];
			if (strcmp(dir_path, "/") == 0)
				snprintf(full, sizeof(full), "/%s", de->d_name);
			else
				snprintf(full, sizeof(full), "%s/%s", dir_path, de->d_name);
			struct stat st;
			is_dir[match_count] = (stat(full, &st) == 0 && S_ISDIR(st.st_mode));
			match_count++;
		}
	}
	closedir(dp);

	if (match_count == 1) {
		const char *match = matches[0];
		const char *suffix = match + prefix_len;
		int s_len = strlen(suffix);
		char append_char = is_dir[0] ? '/' : ' ';

		if (*len + s_len + 1 < SH_LINE_MAX - 2) {
			memmove(&line[cur + s_len + 1], &line[cur], *len - cur);
			memcpy(&line[cur], suffix, s_len);
			line[cur + s_len] = append_char;
			*len += (s_len + 1);
			cur += (s_len + 1);
			line[*len] = '\0';
			*cursor = cur;

			term_write(&line[cur - s_len - 1], *len - (cur - s_len - 1));
			for (int k = 0; k < (*len - cur); k++)
				term_putc('\b');
		}
	} else if (match_count > 1) {
		int common_len = prefix_len;
		int first_len = strlen(matches[0]);
		while (common_len < first_len) {
			char ch = matches[0][common_len];
			int all_match = 1;
			for (int m = 1; m < match_count; m++) {
				if (matches[m][common_len] != ch) {
					all_match = 0;
					break;
				}
			}
			if (!all_match)
				break;
			common_len++;
		}

		if ((size_t)common_len > prefix_len) {
			int add_len = common_len - prefix_len;
			if (*len + add_len < SH_LINE_MAX - 2) {
				memmove(&line[cur + add_len], &line[cur], *len - cur);
				memcpy(&line[cur], matches[0] + prefix_len, add_len);
				*len += add_len;
				cur += add_len;
				line[*len] = '\0';
				*cursor = cur;

				term_write(&line[cur - add_len], *len - (cur - add_len));
				for (int k = 0; k < (*len - cur); k++)
					term_putc('\b');
			}
		} else {
			term_write("\n", 1);
			for (int m = 0; m < match_count; m++) {
				term_write(matches[m], strlen(matches[m]));
				if (is_dir[m])
					term_write("/", 1);
				term_write("  ", 2);
			}
			term_write("\n", 1);
			out2str(getprompt(NULL));
			flushout(out2);
			term_write(line, *len);
			for (int k = *len; k > cur; k--)
				term_putc('\b');
		}
	}
}

static int
interactive_readline(int fd, char *buf, size_t max_size)
{
	struct termios orig_term, raw;
	if (tcgetattr(fd, &orig_term) != 0)
		return read(fd, buf, max_size);

	raw = orig_term;
	raw.c_lflag &= ~(ICANON | ECHO);
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;
	tcsetattr(fd, TCSANOW, &raw);

	char line[SH_LINE_MAX];
	char saved_line[SH_LINE_MAX];
	int len = 0;
	int cursor = 0;
	int hist_idx = s_history_count;
	line[0] = '\0';
	saved_line[0] = '\0';

	while (1) {
		char c;
		ssize_t n = read(fd, &c, 1);
		if (n <= 0) {
			tcsetattr(fd, TCSANOW, &orig_term);
			if (len == 0)
				return 0;
			break;
		}

		/* EOF (Ctrl+D) */
		if (c == 0x04) {
			if (len == 0) {
				tcsetattr(fd, TCSANOW, &orig_term);
				return 0;
			}
			if (cursor < len) {
				memmove(&line[cursor], &line[cursor + 1], len - cursor - 1);
				len--;
				line[len] = '\0';
				term_write(&line[cursor], len - cursor);
				term_putc(' ');
				for (int k = 0; k <= (len - cursor); k++)
					term_putc('\b');
			}
			continue;
		}

		/* Ctrl+C */
		if (c == 0x03) {
			term_write("^C\n", 3);
			tcsetattr(fd, TCSANOW, &orig_term);
			buf[0] = '\n';
			buf[1] = '\0';
			return 1;
		}

		/* Enter (CR or LF) */
		if (c == '\n' || c == '\r') {
			term_putc('\n');
			line[len] = '\0';
			if (len > 0)
				sh_history_add(line);
			break;
		}

		/* Ctrl+L (Clear screen and redraw) */
		if (c == 0x0C) {
			term_write("\033[2J\033[H", 7);
			out2str(getprompt(NULL));
			flushout(out2);
			if (len > 0) {
				term_write(line, len);
				for (int k = len; k > cursor; k--)
					term_putc('\b');
			}
			continue;
		}

		/* Ctrl+U (Clear whole line) */
		if (c == 0x15) {
			while (cursor > 0) {
				term_putc('\b');
				cursor--;
			}
			for (int k = 0; k < len; k++)
				term_putc(' ');
			for (int k = 0; k < len; k++)
				term_putc('\b');
			len = 0;
			cursor = 0;
			line[0] = '\0';
			continue;
		}

		/* Ctrl+K (Kill to end of line) */
		if (c == 0x0B) {
			for (int k = cursor; k < len; k++)
				term_putc(' ');
			for (int k = cursor; k < len; k++)
				term_putc('\b');
			len = cursor;
			line[len] = '\0';
			continue;
		}

		/* Ctrl+A (Beginning of line) */
		if (c == 0x01) {
			while (cursor > 0) {
				term_putc('\b');
				cursor--;
			}
			continue;
		}

		/* Ctrl+E (End of line) */
		if (c == 0x05) {
			while (cursor < len) {
				term_putc(line[cursor]);
				cursor++;
			}
			continue;
		}

		/* Backspace */
		if (c == '\b' || c == 0x7F) {
			if (cursor > 0) {
				memmove(&line[cursor - 1], &line[cursor], len - cursor);
				cursor--;
				len--;
				line[len] = '\0';
				term_putc('\b');
				term_write(&line[cursor], len - cursor);
				term_putc(' ');
				for (int k = 0; k <= (len - cursor); k++)
					term_putc('\b');
			}
			continue;
		}

		/* Tab completion */
		if (c == '\t') {
			sh_tab_complete(line, &len, &cursor);
			continue;
		}

		/* Escape Sequences (Arrows, Home, End, Delete) */
		if (c == '\033') {
			struct pollfd pfd;
			pfd.fd = fd;
			pfd.events = POLLIN;
			pfd.revents = 0;
			if (poll(&pfd, 1, 50) <= 0 || !(pfd.revents & POLLIN)) {
				/* Standalone Escape key */
				continue;
			}
			char seq[4] = {0};
			if (read(fd, &seq[0], 1) > 0) {
				if (seq[0] == '[' || seq[0] == 'O') {
					if (read(fd, &seq[1], 1) > 0) {
						if (seq[1] >= '0' && seq[1] <= '9') {
							(void)read(fd, &seq[2], 1);
						}

						/* Up Arrow */
						if ((seq[0] == '[' || seq[0] == 'O') && seq[1] == 'A') {
							if (s_history_count > 0 && hist_idx > 0) {
								if (hist_idx == s_history_count) {
									strncpy(saved_line, line, sizeof(saved_line) - 1);
									saved_line[sizeof(saved_line) - 1] = '\0';
								}
								hist_idx--;
								const char *hist = s_history[hist_idx % SH_HIST_MAX];
								while (cursor > 0) {
									term_putc('\b');
									cursor--;
								}
								for (int k = 0; k < len; k++)
									term_putc(' ');
								for (int k = 0; k < len; k++)
									term_putc('\b');
								strncpy(line, hist, sizeof(line) - 1);
								line[sizeof(line) - 1] = '\0';
								len = strlen(line);
								cursor = len;
								term_write(line, len);
							}
							continue;
						}

						/* Down Arrow */
						if ((seq[0] == '[' || seq[0] == 'O') && seq[1] == 'B') {
							if (hist_idx < s_history_count) {
								hist_idx++;
								const char *src = (hist_idx == s_history_count) ?
								    saved_line : s_history[hist_idx % SH_HIST_MAX];
								while (cursor > 0) {
									term_putc('\b');
									cursor--;
								}
								for (int k = 0; k < len; k++)
									term_putc(' ');
								for (int k = 0; k < len; k++)
									term_putc('\b');
								strncpy(line, src, sizeof(line) - 1);
								line[sizeof(line) - 1] = '\0';
								len = strlen(line);
								cursor = len;
								term_write(line, len);
							}
							continue;
						}

						/* Left Arrow */
						if ((seq[0] == '[' || seq[0] == 'O') && seq[1] == 'D') {
							if (cursor > 0) {
								term_putc('\b');
								cursor--;
							}
							continue;
						}

						/* Right Arrow */
						if ((seq[0] == '[' || seq[0] == 'O') && seq[1] == 'C') {
							if (cursor < len) {
								term_putc(line[cursor]);
								cursor++;
							}
							continue;
						}

						/* Home */
						if ((seq[0] == '[' && (seq[1] == 'H' || (seq[1] == '1' && seq[2] == '~') || (seq[1] == '7' && seq[2] == '~'))) ||
						    (seq[0] == 'O' && seq[1] == 'H')) {
							while (cursor > 0) {
								term_putc('\b');
								cursor--;
							}
							continue;
						}

						/* End */
						if ((seq[0] == '[' && (seq[1] == 'F' || (seq[1] == '4' && seq[2] == '~') || (seq[1] == '8' && seq[2] == '~'))) ||
						    (seq[0] == 'O' && seq[1] == 'F')) {
							while (cursor < len) {
								term_putc(line[cursor]);
								cursor++;
							}
							continue;
						}

						/* Delete (ESC [ 3 ~) */
						if (seq[0] == '[' && seq[1] == '3' && seq[2] == '~') {
							if (cursor < len) {
								memmove(&line[cursor], &line[cursor + 1], len - cursor - 1);
								len--;
								line[len] = '\0';
								term_write(&line[cursor], len - cursor);
								term_putc(' ');
								for (int k = 0; k <= (len - cursor); k++)
									term_putc('\b');
							}
							continue;
						}
					}
				}
			}
			continue;
		}

		/* Regular printable character */
		if ((unsigned char)c >= 32 && len < (int)sizeof(line) - 2) {
			if (cursor == len) {
				line[len++] = c;
				cursor++;
				line[len] = '\0';
				term_putc(c);
			} else {
				memmove(&line[cursor + 1], &line[cursor], len - cursor);
				line[cursor] = c;
				len++;
				cursor++;
				line[len] = '\0';
				term_write(&line[cursor - 1], len - cursor + 1);
				for (int k = 0; k < (len - cursor); k++)
					term_putc('\b');
			}
		}
	}

	tcsetattr(fd, TCSANOW, &orig_term);

	if ((size_t)len + 2 > max_size)
		len = (int)max_size - 2;

	memcpy(buf, line, len);
	buf[len] = '\n';
	buf[len + 1] = '\0';
	return len + 1;
}

static int
preadfd(void)
{
	int nr;

retry:
#ifndef NO_HISTORY
	if (parsefile->fd == 0 && el) {
		const char *line;

		el_resize(el);
		line = el_gets(el, &nr);
		if (nr > 0 && parsefile->bufsize < (size_t)nr + 1) {
			size_t bufsize;

			INTOFF;
			if (parsefile->buf != basebuf) {
				ckfree(parsefile->buf);
				parsefile->buf = NULL;
				parsefile->bufsize = 0;
			}
			bufsize = (size_t)nr + BUFSIZ + 1;
			bufsize -= bufsize % BUFSIZ;
			parsefile->buf = ckmalloc(bufsize);
			parsefile->bufsize = bufsize;
			INTON;
		}
		if (nr > 0 && line != NULL)
			memcpy(parsefile->buf, line, nr);
		else
			nr = nr ? -1 : 0;
	} else
#endif
	if (parsefile->fd == 0 && iflag && isatty(0)) {
		nr = interactive_readline(0, parsefile->buf, parsefile->bufsize - 1);
	} else {
		nr = read(parsefile->fd, parsefile->buf, parsefile->bufsize - 1);
	}

	if (nr < 0)
		switch (errno) {
			int flags;

		case EINTR:
			goto retry;
		case EWOULDBLOCK:
			if (parsefile->fd != 0)
				break;
			if ((flags = fcntl(0, F_GETFL, 0)) < 0)
				break;
			if (!(flags & O_NONBLOCK))
				break;
			if (fcntl(0, F_SETFL, flags & ~O_NONBLOCK) < 0)
				break;
			out2fmt_flush("sh: turning off NDELAY mode\n");
			goto retry;
                }
	else if (nr > 0)
		parsefile->buf[nr] = '\0';
	else
		nr = -1;

	parsenextc = parsefile->buf;
	return nr;
}

/*
 * Refill the input buffer and return the next input character:
 *
 * 1) If a string was pushed back on the input, pop it;
 * 2) If an EOF was pushed back (parsenleft == EOF_NLEFT) or we are reading
 *    from a string so we can't refill the buffer, return EOF.
 * 3) If there is more in this buffer, use it else call read to fill it.
 * 4) Process input up to the next newline, deleting nul characters.
 */

int
preadbuffer(void)
{
	const char *end;
	char *q, *r;
	char savec;

	while (parsefile->strpush) {
		/*
		 * Add a space to the end of an alias to ensure that the
		 * alias remains in use while parsing its last word.
		 * This avoids alias recursions.
		 */
		if (parsenleft == -1 && parsefile->strpush->ap != NULL)
			return ' ';
		popstring();
		if (--parsenleft >= 0)
			return (*parsenextc++);
	}
	if (parsenleft == EOF_NLEFT || parsefile->buf == NULL)
		return PEOF;

again:
	if (parselleft <= 0 && (parselleft = preadfd()) == -1) {
		parselleft = parsenleft = EOF_NLEFT;
		return (PEOF);
	}
	end = parsenextc + parselleft;
	q = strchrnul(parsenextc, '\n');
	if (*q == '\0' && q != end) {
		/* delete nul characters */
		for (r = q++; q != end; q++)
			if (*q != '\0')
				*r++ = *q;
		*r = '\0';
		parselleft = r - parsenextc;
		goto again;
	}
	if (*q == '\0') {
		parsenleft = parselleft;
		parselleft = 0;
	} else /* *q == '\n' */ {
		q++;
		parsenleft = q - parsenextc;
		parselleft -= parsenleft;
	}
	parsenleft--;

	savec = *q;
	*q = '\0';

#ifndef NO_HISTORY
	if (parsefile->fd == 0 && hist &&
	    parsenextc[strspn(parsenextc, " \t\n")] != '\0') {
		HistEvent he;
		INTOFF;
		history(hist, &he, whichprompt == 1 ? H_ENTER : H_ADD,
		    parsenextc);
		INTON;
	}
#endif

	if (vflag) {
		out2str(parsenextc);
		flushout(out2);
	}

	*q = savec;

	return *parsenextc++;
}

/*
 * Returns if we are certain we are at EOF. Does not cause any more input
 * to be read from the outside world.
 */

int
preadateof(void)
{
	if (parsenleft > 0)
		return 0;
	if (parsefile->strpush)
		return 0;
	if (parsenleft == EOF_NLEFT || parsefile->buf == NULL)
		return 1;
	return 0;
}

/*
 * Undo the last call to pgetc.  Only one character may be pushed back.
 * PEOF may be pushed back.
 */

void
pungetc(void)
{
	parsenleft++;
	parsenextc--;
}

/*
 * Push a string back onto the input at this current parsefile level.
 * We handle aliases this way.
 */
void
pushstring(const char *s, int len, struct alias *ap)
{
	struct strpush *sp;

	INTOFF;
/*out2fmt_flush("*** calling pushstring: %s, %d\n", s, len);*/
	if (parsefile->strpush) {
		sp = ckmalloc(sizeof(struct strpush));
		sp->prev = parsefile->strpush;
		parsefile->strpush = sp;
	} else
		sp = parsefile->strpush = &(parsefile->basestrpush);
	sp->prevstring = parsenextc;
	sp->prevnleft = parsenleft;
	sp->prevlleft = parselleft;
	sp->ap = ap;
	if (ap)
		ap->flag |= ALIASINUSE;
	parsenextc = s;
	parsenleft = len;
	INTON;
}

static void
popstring(void)
{
	struct strpush *sp = parsefile->strpush;

	INTOFF;
	if (sp->ap) {
		if (parsenextc != sp->ap->val &&
		    (parsenextc[-1] == ' ' || parsenextc[-1] == '\t'))
			forcealias();
		sp->ap->flag &= ~ALIASINUSE;
	}
	parsenextc = sp->prevstring;
	parsenleft = sp->prevnleft;
	parselleft = sp->prevlleft;
/*out2fmt_flush("*** calling popstring: restoring to '%s'\n", parsenextc);*/
	parsefile->strpush = sp->prev;
	if (sp != &(parsefile->basestrpush))
		ckfree(sp);
	INTON;
}

/*
 * Set the input to take input from a file.  If push is set, push the
 * old input onto the stack first.
 * About verify:
 *   -1: Obey verifyflag
 *    0: Do not verify
 *    1: Do verify
 */

void
setinputfile(const char *fname, int push, int verify)
{
	int e;
	int fd;
	int fd2;
	int oflags = O_RDONLY | O_CLOEXEC;

	if (verify == 1 || (verify == -1 && verifyflag))
		oflags |= O_VERIFY;

	INTOFF;
	if ((fd = open(fname, oflags)) < 0) {
		e = errno;
		errorwithstatus(e == ENOENT || e == ENOTDIR ? 127 : 126,
		    "cannot open %s: %s", fname, strerror(e));
	}
	if (fd < 10) {
		fd2 = fcntl(fd, F_DUPFD_CLOEXEC, 10);
		close(fd);
		if (fd2 < 0)
			error("Out of file descriptors");
		fd = fd2;
	}
	setinputfd(fd, push);
	INTON;
}


/*
 * Like setinputfile, but takes an open file descriptor (which should have
 * its FD_CLOEXEC flag already set).  Call this with interrupts off.
 */

void
setinputfd(int fd, int push)
{
	if (push)
		pushfile();
	if (parsefile->fd > 0)
		close(parsefile->fd);
	parsefile->fd = fd;
	if (parsefile->buf == NULL) {
		parsefile->buf = ckmalloc(BUFSIZ + 1);
		parsefile->bufsize = BUFSIZ + 1;
	}
	parselleft = parsenleft = 0;
	plinno = 1;
}


/*
 * Like setinputfile, but takes input from a string.
 */

void
setinputstring(const char *string)
{
	INTOFF;
	pushfile();
	parsenextc = string;
	parselleft = parsenleft = strlen(string);
	plinno = 1;
	INTON;
}



/*
 * To handle the "." command, a stack of input files is used.  Pushfile
 * adds a new entry to the stack and popfile restores the previous level.
 */

static void
pushfile(void)
{
	struct parsefile *pf;

	pf = (struct parsefile *)ckmalloc(sizeof(struct parsefile));
	*pf = (struct parsefile){ .prev = parsefile, .fd = -1 };
	parsefile->nleft = parsenleft;
	parsefile->lleft = parselleft;
	parsefile->nextc = parsenextc;
	parsefile->linno = plinno;
	parsefile = pf;
}


void
popfile(void)
{
	struct parsefile *pf = parsefile;

	INTOFF;
	if (pf->fd >= 0)
		close(pf->fd);
	if (pf->buf)
		ckfree(pf->buf);
	while (pf->strpush)
		popstring();
	parsefile = pf->prev;
	ckfree(pf);
	parsenleft = parsefile->nleft;
	parselleft = parsefile->lleft;
	parsenextc = parsefile->nextc;
	plinno = parsefile->linno;
	INTON;
}


/*
 * Return current file (to go back to it later using popfilesupto()).
 */

struct parsefile *
getcurrentfile(void)
{
	return parsefile;
}


/*
 * Pop files until the given file is on top again. Useful for regular
 * builtins that read shell commands from files or strings.
 * If the given file is not an active file, an error is raised.
 */

void
popfilesupto(struct parsefile *file)
{
	while (parsefile != file && parsefile != &basepf)
		popfile();
	if (parsefile != file)
		error("popfilesupto() misused");
}

/*
 * Return to top level.
 */

void
popallfiles(void)
{
	while (parsefile != &basepf)
		popfile();
}



/*
 * Close the file(s) that the shell is reading commands from.  Called
 * after a fork is done.
 */

void
closescript(void)
{
	popallfiles();
	if (parsefile->fd > 0) {
		close(parsefile->fd);
		parsefile->fd = 0;
	}
}
