/* MIT License

Copyright (c) 2010 Vladimir Antonov

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>

#define _GNU_SOURCE
#define __USE_GNU

#include <string.h>
#include <termios.h>
#include <errno.h>
#include <syslog.h>
#include <signal.h>
#include <ctype.h>

#ifndef TIOCGWINSZ
# include <sys/ioctl.h>
#endif
#include <sys/time.h>

#include "config.h"

#include "readline.h"

#ifdef RL_HISTORY_FILE
# include <sys/stat.h>
# include <fcntl.h>
#endif

/*
[*] terminal control (init/deinit)
[*] commands table // do/undo pairs
[*] commands dispatcher
[*] commands handlers
[*] history control
[ ] undo buffer
*/

#define STATIC static

#define CUR_LEFT   "\b"
#define CUR_HOME   "\r"

#define CUR_UP_N    "\033[%uA"
#define CUR_DOWN_N  "\033[%uB"
#define CUR_RIGHT_N "\033[%uC"
#define CUR_LEFT_N  "\033[%uD"

#define SET_WRAP_MODE "\033[?7h"

/* -------------------------------------------------------------------------- */
#ifndef RL_USE_WRITE

#define rl_out_purge() do { fflush(stdout); } while(0)
#define rl_out(data, size) do { fwrite(data, size, 1, stdout); } while(0)
#define rl_printf(...) do { printf(__VA_ARGS__); } while(0)

#else

static void rl_out(char const *data, int size);
static void rl_out_purge();
static void rl_printf(char const *fmt, ...);

#endif

#ifndef countof
# define countof(arr)  (sizeof(arr)/sizeof(arr[0]))
#endif


/* -------------------------------------------------------------------------- */
typedef unsigned int rl_glyph_t;

/* -------------------------------------------------------------------------- */
typedef 
struct rl_history {
	char *line;
	char *lines[RL_HISTORY_HEIGHT];
	int size, current;
} rl_history_t;

/* -------------------------------------------------------------------------- */
typedef 
struct _rl_state {
	char raw[RL_MAX_LENGTH];         /* utf-8 */
	rl_glyph_t line[RL_MAX_LENGTH];  /* unicode */
	int length, cur_pos;             /* length and position in glyphs */

	int finish;
	rl_history_t history;

	char const *prompt;
	int prompt_width;
	rl_get_completion_fn *_get_completion;
} rl_state_t;

/* -------------------------------------------------------------------------- */
struct _rl_command {
	char const seq[8];
	void (*handler)();
};

/* -------------------------------------------------------------------------- */
static rl_state_t *rl_state;

/* -------------------------------------------------------------------------- */
STATIC void rl_insert_seq(char const *seq);

#ifdef RL_WINDOW_WIDTH

struct {
	int cols;

	int need_update, need_redraw;
	struct sigaction old_sigwinch, old_sigalrm;
} rl_window;

static void rl_update_window();

/* -------------------------------------------------------------------------- */
static void sig_alarm(int sig)
{
}
/* -------------------------------------------------------------------------- */
static void sig_winch(int sig)
{
	rl_window.need_update = 1;
	static const struct itimerval itmr = {
		.it_interval = { .tv_sec = 0, .tv_usec = 0 },
		.it_value = { .tv_sec = 0, .tv_usec = 100000 }
	};
	setitimer(ITIMER_REAL, &itmr, NULL);
}

/* -------------------------------------------------------------------------- */
static inline int rl_window_check()
{
	return rl_window.need_update;
}

/* -------------------------------------------------------------------------- */
static void rl_window_update()
{
	struct winsize ws;
	if (rl_window.need_update) {
		rl_window.need_update = 0;
		rl_window.cols = (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) ?
			RL_WINDOW_WIDTH : ws.ws_col;
	}
}

typedef
void (sig_handler_t)(int);

/* -------------------------------------------------------------------------- */
static int rl_signal(int sig, int flags, sig_handler_t *handler, struct sigaction *old)
{
	if (!handler)
		return sigaction(sig, old, NULL);

	struct sigaction act;
	act.sa_flags = flags;
	sigemptyset(&act.sa_mask);
	act.sa_handler = handler;
	return sigaction(sig, &act, old);
}

/* -------------------------------------------------------------------------- */
static void rl_window_init()
{
	rl_window.need_update = 1;
	rl_window_update();
	rl_signal(SIGWINCH, SA_RESTART, sig_winch, &rl_window.old_sigwinch);
	rl_signal(SIGALRM, 0, sig_alarm, &rl_window.old_sigalrm);
}

/* -------------------------------------------------------------------------- */
static void rl_window_free()
{
	if (rl_window.old_sigwinch.sa_handler)
		rl_signal(SIGWINCH, 0, NULL, &rl_window.old_sigwinch);
	if (rl_window.old_sigalrm.sa_handler)
		rl_signal(SIGALRM, 0, NULL, &rl_window.old_sigalrm);
}
#else
static struct {
	int cols;
} rl_window = { 80 };

/* -------------------------------------------------------------------------- */
static inline  int rl_window_check(){ return 0; }
static inline void rl_window_update() {}
static inline void rl_window_init()  {}
static inline void rl_window_free()  {}
#endif

/* -------------------------------------------------------------------------- */
static int atexit_ok = 0;
static int in_raw = 0;
static struct termios term_old;

/* -------------------------------------------------------------------------- */
STATIC int rl_term_unraw()
{
	if (!in_raw)
		return 0;

	return in_raw = tcsetattr(STDOUT_FILENO, TCSAFLUSH, &term_old);
}

/* -------------------------------------------------------------------------- */
STATIC void rl_atexit()
{
	rl_term_unraw();
}

/* -------------------------------------------------------------------------- */
STATIC int rl_term_raw()
{
	if (!atexit_ok)
		atexit_ok = !atexit(rl_atexit);
	if (tcgetattr(STDOUT_FILENO, &term_old) < 0)
		return -1;

	struct termios my = term_old;
	my.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON); //~(ICRNL | INPCK | ISTRIP | IXON);
	my.c_iflag |=  (IGNBRK);
	my.c_oflag &= ~(OPOST);
	my.c_cflag |=  (CS8);
	my.c_lflag &= ~(ECHO | ICANON | IEXTEN); // | ISIG);
	my.c_cc[VMIN] = 1;
	my.c_cc[VTIME] = 0;

	if (tcsetattr(STDOUT_FILENO, TCSAFLUSH, &my) < 0)
		return -1;

	return in_raw = 1, 0;
}

/* ------------------------------------<------------------------------------- */
STATIC void safe_write(int fd, char const *data, int size)
{
	if (!size)
		return;

	int ret;
_next_part:
	do {
		ret = write(fd, data, size);
	} while (ret < 0 && errno == EINTR);

	if (ret < 0) {
		syslog(LOG_DEBUG, "readline write: %m");
		exit(1);
	}
	if (ret < size) {
		data += ret;
		size -= ret;
		goto _next_part;
	}
}

/* ------------------------------------<------------------------------------- */
STATIC int safe_read(int fd, char *data, int size)
{
	int ret;
	do {
		ret = read(fd, data, size);
		if (ret >= 0 || errno != EINTR)
			break;
		rl_update_window();
	} while (1);

	if (ret < 0) {
		syslog(LOG_DEBUG, "readline read: %m");
		exit(1);
	}
	return ret;
}

#ifdef RL_USE_WRITE
/* -------------------------------------------------------------------------- */
static struct {
	unsigned char data[4096];
	int top;
} rl_output;

/* -------------------------------------------------------------------------- */
void rl_out_purge()
{
	if (!rl_output.top)
		return;

	safe_write(STDOUT_FILENO, rl_output.data, rl_output.top);
	rl_output.top = 0;
}

/* -------------------------------------------------------------------------- */
void rl_out(char const *data, int size)
{
	if (rl_output.top && (rl_output.top + size) > sizeof(rl_output.data)) {
		unsigned int dsize = sizeof(rl_output.data) - rl_output.top;
		memcpy(rl_output.data + rl_output.top, data, dsize);
		data += dsize;
		size -= dsize;
		rl_output.top += dsize;
		rl_out_purge();
	}

	if (!rl_output.top)
		while (size > sizeof(rl_output.data)) {
			safe_write(STDOUT_FILENO, data, sizeof(rl_output.data));
			size -= sizeof(rl_output.data);
			data += sizeof(rl_output.data);
		}

	memcpy(rl_output.data + rl_output.top, data, size);
	rl_output.top += size;
}

/* ------------------------------------<------------------------------------- */
void rl_printf(char const *fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	char outbuf[1024];
	int length = vsnprintf(outbuf, sizeof(outbuf), fmt, va);
	va_end(va);
	rl_out(outbuf, length);
}
#endif

/* -------------------------------------------------------------------------- */
static inline rl_glyph_t utf8_to_glyph(char const **utf8)
{
	rl_glyph_t glyph;
	char const *raw = *utf8;
	if (!(*raw & 0x80))
		glyph = *raw++;
	else
		if ((*raw & 0xE0) == 0xC0) {
			if ((raw[1] & 0300) == 0200) {
				glyph = ((raw[0] & 0x1F) << 6) + (raw[1] & 0x3F);
				raw += 2;
			} else
				return 0;
		} else
			if ((*raw & 0xF0) == 0xE0) {
				if ((raw[1] & 0300) == 0200 && (raw[2] & 0300) == 0200) {
					glyph = ((raw[0] & 0xF) << 12) + ((raw[1] & 0x3F) << 6) + (raw[2] & 0x3F);
					raw += 3;
				} else
					return 0;
			}
	*utf8 = raw;
	return glyph;
}

/* ------------------------------------<------------------------------------- */
STATIC rl_glyph_t *utf8tog(rl_glyph_t *glyphs, char const *raw)
{
	while (*raw) {
		rl_glyph_t gl = utf8_to_glyph(&raw);
		if (gl)
			*glyphs++ = gl;
		else
			++raw; /* skip a char of wrong utf8 sequence */
	}

	*glyphs = 0;
	return glyphs;
}

/* -------------------------------------------------------------------------- */
STATIC int utf8_width(char const *raw)
{
	int count = 0;
	while (*raw) {
		if (!utf8_to_glyph(&raw))
			++raw; /* skip a char of wrong utf8 sequence */
		++count;
	}

	return count;
}

/* ----------------------------<--------------------------------------------- */
STATIC char *gtoutf8(char *raw, rl_glyph_t const *glyphs, int count)
{
	while (*glyphs && count--) {
		unsigned int uc = *glyphs++;
		if (!(uc & 0xFF80))
			*raw++ = uc;
		else
			if (!(uc & 0xF800)) {
				*raw++ = 0xC0 | (uc >> 6);
				*raw++ = 0x80 | (uc & 0x3F);
			} else {
				*raw++ = 0xE0 | (uc >> 12);
				*raw++ = 0x80 | (0x3F & uc >> 6);
				*raw++ = 0x80 | (0x3F & uc);
			}
	}
	*raw = 0;
	return raw;
}

/* -------------------------------------------------------------------------- */
STATIC int skip_char_seq(char const *start)
{
	char const *raw = start;
	rl_glyph_t glyph = utf8_to_glyph(&raw);
	if (glyph != '\033')
		return raw - start;

	unsigned char ch = *raw++;
	switch (ch) {
	case '[':
	case 'O':
		while (isdigit(*raw) || *raw == ';')
			++raw;
		ch = *raw++;
		if (64 <= ch && ch <= 126)
			return raw - start;
		return 0;

	default:
		if (32 <= ch && ch <= 127)
			return raw + 1 - start;
	}

	return 0; /* not closed sequence */
}

/* -------------------------------------------------------------------------- */
STATIC void rl_write(char const *code, int count)
{
	char buf[RL_MAX_LENGTH*2], *to = buf;
	while (count--)
		to = stpcpy(to, code);
	rl_out(buf, to-buf);
}

/* -------------------------------------------------------------------------- */
STATIC void rl_move(int count)
{
	if (!rl_window.cols) {
		if (count < 0)
			rl_write(CUR_LEFT, -count);
		else
			if (count > 0) {
				char buf[RL_MAX_LENGTH * 3];
				char *to = gtoutf8(buf, rl_state->line + rl_state->cur_pos, count);
				if (to != buf)
					rl_out(buf, to - buf);
			}
		return;
	}

	int pos = rl_state->cur_pos + rl_state->prompt_width;
	int row = pos / rl_window.cols;
	int col = pos % rl_window.cols;
	pos += count;
	int torow = pos / rl_window.cols;
	int tocol = pos % rl_window.cols;

	if (tocol < col)
		rl_printf(CUR_LEFT_N, col - tocol);
	else
		if (tocol > col)
			rl_printf(CUR_RIGHT_N, tocol - col);

	if (torow < row)
		rl_printf(CUR_UP_N, row - torow);
	else
		if (torow > row)
			rl_printf(CUR_DOWN_N, torow - row);
}

/* -------------------------------------------------------------------------- */
STATIC void rl_update_tail(int afterspace)
{
	int c = afterspace;
	char buf[RL_MAX_LENGTH * 3];
	char *to = gtoutf8(buf, rl_state->line + rl_state->cur_pos, -1);
	while (c--)
		*to++ = ' ';
	*to = 0;
	if (to != buf)
		rl_out(buf, to-buf);
	c = afterspace + rl_state->length - rl_state->cur_pos;
	if (c > 0) {
		rl_state->cur_pos += c;
		rl_move(-c);
		rl_state->cur_pos -= c;
	}
}

/* -------------------------------------------------------------------------- */
STATIC void rl_write_part(int start, int length)
{
	char buf[RL_MAX_LENGTH*2];
	char *to = gtoutf8(buf, rl_state->line + start, length);
	if (to != buf)
		rl_out(buf, to-buf);
}

/* -------------------------------------------------------------------------- */
STATIC void rl_redraw(int inplace, int tail)
{
	if (inplace)
		rl_move(-(rl_state->cur_pos + rl_state->prompt_width));

	rl_out(rl_state->prompt, strlen(rl_state->prompt));
	rl_write_part(0, rl_state->cur_pos);
	rl_update_tail(tail);
}

/* -------------------------------------------------------------------------- */
STATIC void rlc_cursor_home()
{
	rl_move(-rl_state->cur_pos);
	rl_state->cur_pos = 0;
}

/* -------------------------------------------------------------------------- */
STATIC void rl_set_text(char const *text, int redraw)
{
	if (redraw)
		rlc_cursor_home();

	int oldlen = rl_state->length;

	strncpy(rl_state->raw, text, sizeof(rl_state->raw) - 1);
	rl_state->raw[sizeof(rl_state->raw) - 1] = 0;
	rl_glyph_t *end = utf8tog(rl_state->line, rl_state->raw);
	rl_state->length = rl_state->cur_pos = end - rl_state->line;

	if (redraw) {
		rl_write_part(0, rl_state->length);
		if (oldlen > rl_state->length)
			rl_update_tail(oldlen - rl_state->length);
	}
}

/* -------------------------------------------------------------------------- */
STATIC void history_pop(int idx)
{
	rl_history_t *h = &rl_state->history;

	if (idx == h->size)
		if (h->line) {
			rl_set_text(h->line, 1);
			free(h->line); h->line = NULL;
			return ;
		}

	if (idx >= h->size)
		return;

	if (!h->line) {
		gtoutf8(rl_state->raw, rl_state->line, -1);
		h->line = strdup(rl_state->raw);
	}

	rl_set_text(h->lines[idx], 1);
}

/* -------------------------------------------------------------------------- */
STATIC void rlc_cursor_end()
{
	rl_write_part(rl_state->cur_pos, -1);
	rl_state->cur_pos = rl_state->length;
}

/* -------------------------------------------------------------------------- */
STATIC void rlc_cursor_left()
{
	if (rl_state->cur_pos) {
		rl_move(-1);
		--rl_state->cur_pos;
	}
}

/* -------------------------------------------------------------------------- */
STATIC void rlc_cursor_right()
{
	if (rl_state->cur_pos < rl_state->length)
		rl_write_part(rl_state->cur_pos++, 1);
}

/* -------------------------------------------------------------------------- */
STATIC void rlc_cursor_word_left()
{
	if (!rl_state->cur_pos)
		return;

	unsigned int pos = rl_state->cur_pos;
	rl_glyph_t *line = rl_state->line;
	while (pos && rl_state->line[pos-1] == ' ')
		--pos;

	while (pos && rl_state->line[pos-1] != ' ')
		--pos;

	rl_move(pos - rl_state->cur_pos);
	rl_state->cur_pos = pos;
}

/* -------------------------------------------------------------------------- */
STATIC int rlc_next_word()
{
	unsigned int pos = rl_state->cur_pos, length = rl_state->length;
	rl_glyph_t *line = rl_state->line;
	while (pos < length && rl_state->line[pos] != ' ')
		++pos;

	while (pos < length && rl_state->line[pos] == ' ')
		++pos;
	return pos;
}

/* -------------------------------------------------------------------------- */
STATIC void rlc_cursor_word_right()
{
	if (rl_state->cur_pos >= rl_state->length)
		return;

	unsigned int pos = rlc_next_word();
	rl_write_part(rl_state->cur_pos, pos - rl_state->cur_pos);
	rl_state->cur_pos = pos;
}

/* -------------------------------------------------------------------------- */
STATIC void rlc_delete_n(int count)
{
	if (rl_state->cur_pos < rl_state->length && count) {
		int tail = (rl_state->length - rl_state->cur_pos);
		if (count > tail)
			count = tail;
		memmove(
			rl_state->line + rl_state->cur_pos,
			rl_state->line + rl_state->cur_pos + count,
			(rl_state->length - rl_state->cur_pos - count + 1) * sizeof(rl_glyph_t));
		rl_state->length -= count;
		rl_update_tail(count);
	}
}

/* -------------------------------------------------------------------------- */
STATIC void rlc_delete()
{
	rlc_delete_n(1);
}

/* -------------------------------------------------------------------------- */
STATIC void rlc_backspace()
{
	if (rl_state->cur_pos) {
		rl_move(-1);
		--rl_state->cur_pos;
		rlc_delete_n(1);
	}
}

/* -------------------------------------------------------------------------- */
STATIC void rlc_backword()
{
	int end = rl_state->cur_pos;
	rlc_cursor_word_left();
	rlc_delete_n(end - rl_state->cur_pos);
}

/* -------------------------------------------------------------------------- */
STATIC void rlc_delete_word()
{
	unsigned int end = rlc_next_word();
	rlc_delete_n(end - rl_state->cur_pos);
}

/* -------------------------------------------------------------------------- */
STATIC void rlc_delete_to_begin()
{
	int len = rl_state->cur_pos;
	rlc_cursor_home();
	rlc_delete_n(len);
}

/* -------------------------------------------------------------------------- */
STATIC void rlc_delete_to_end()
{
	rlc_delete_n(rl_state->length - rl_state->cur_pos);
}

/* -------------------------------------------------------------------------- */
STATIC void rlc_history_back()
{
	rl_history_t *h = &rl_state->history;
	if (h->current)
		history_pop(--(h->current));
}

/* -------------------------------------------------------------------------- */
STATIC void rlc_history_forward()
{
	rl_history_t *h = &rl_state->history;
	if (h->current < h->size)
		history_pop(++(h->current));
}

/* -------------------------------------------------------------------------- */
STATIC void rlc_history_begin()
{
	rl_history_t *h = &rl_state->history;
	if (h->current)
		history_pop((h->current = 0));
}

/* -------------------------------------------------------------------------- */
STATIC void rlc_history_end()
{
	rl_history_t *h = &rl_state->history;
	if (h->current < h->size)
		history_pop((h->current = h->size));
}

/* -------------------------------------------------------------------------- */
STATIC void rlc_enter()
{
	rl_state->finish = 1;
}

/* -------------------------------------------------------------------------- */
STATIC void rlc_autocomplete()
{
	if (!rl_state->_get_completion)
		return;

	char *start = rl_state->raw;
	char *cur_pos = gtoutf8(start, rl_state->line, rl_state->cur_pos);
	char *end = gtoutf8(cur_pos, rl_state->line + rl_state->cur_pos, rl_state->length - rl_state->cur_pos);

	char const *insert = (rl_state->_get_completion)(start, cur_pos);
	if (insert)
		rl_insert_seq(insert);
}

/* -------------------------------------------------------------------------- */
static const struct _rl_command rl_commands[] = {
	{ "\001",      rlc_cursor_home },
	{ "\002",      rlc_cursor_left },
	{ "\006",      rlc_cursor_right },
	{ "\005",      rlc_cursor_end },
	{ "\033b",     rlc_cursor_word_left },
	{ "\033f",     rlc_cursor_word_right },
	{ "\010",      rlc_backspace },
	{ "\004",      rlc_delete },
	{ "\027",      rlc_backword },
	{ "\033d",     rlc_delete_word },
	{ "\013",      rlc_delete_to_end },
	{ "\025",      rlc_delete_to_begin },
	{ "\t",        rlc_autocomplete },
	{ "\020",      rlc_history_back },
	{ "\016",      rlc_history_forward },
	{ "\033<",     rlc_history_begin },
	{ "\033>",     rlc_history_end },

/* VT100 */
	{ "\033OH",    rlc_cursor_home },
	{ "\033OF",    rlc_cursor_end },
	{ "\033[A",    rlc_history_back },
	{ "\033[B",    rlc_history_forward },
	{ "\033[D",    rlc_cursor_left },
	{ "\033[C",    rlc_cursor_right },
	{ "\033[1;5D", rlc_cursor_word_left },
	{ "\033[1;5C", rlc_cursor_word_right },
	{ "\033[3~",   rlc_delete },
	{ "\x7F",      rlc_backspace },

/* PuTTY */
	{ "\033[1~",   rlc_cursor_home },
	{ "\033[4~",   rlc_cursor_end },
	{ "\033OD",    rlc_cursor_word_left },
	{ "\033OC",    rlc_cursor_word_right },

/* Hyper Terminal */
	{ "\033[H",    rlc_cursor_home },
	{ "\033[K",    rlc_cursor_end },

/* VT52 */
	{ "\033H",     rlc_cursor_home },
	{ "\033A",     rlc_history_back },
	{ "\033B",     rlc_history_forward },
	{ "\033D",     rlc_cursor_left },
	{ "\033C",     rlc_cursor_right },
	{ "\033K",     rlc_delete_to_end },

	{ "\n",        rlc_enter },
	{ "\r",        rlc_enter }
};

/* -------------------------------------------------------------------------- */
void rl_insert_seq(char const *seq)
{
	rl_glyph_t uc[RL_MAX_LENGTH*2];
	rl_glyph_t *end = utf8tog(uc, seq);
	int count = end - uc;
	int max_count = countof(rl_state->line) - rl_state->length - 1;
	if (count > max_count)
		count = max_count;

	if (!count)
		return;

	if (rl_state->length - rl_state->cur_pos)
		memmove(
			rl_state->line + rl_state->cur_pos + count, 
			rl_state->line + rl_state->cur_pos, 
			sizeof(rl_state->line[0]) * (rl_state->length - rl_state->cur_pos));

	memcpy(
		rl_state->line + rl_state->cur_pos, 
		uc,
		sizeof(uc[0]) * count);

	rl_state->length += count;
	rl_state->line[rl_state->length] = 0;

	rl_write_part(rl_state->cur_pos, count);
	rl_state->cur_pos += count;
	rl_update_tail(0);
}

/* -------------------------------------------------------------------------- */
STATIC int rl_exec_seq(char *seq)
{
	const struct _rl_command *cmd = rl_commands, *end = rl_commands + countof(rl_commands);

	while (cmd < end) {
		if (!strcmp(cmd->seq, seq)) {
			cmd->handler();
			goto _exit;
		}
		++cmd;
	}

	if (seq[0] & 0xE0)
		rl_insert_seq(seq);

_exit:
	rl_out_purge();
	return rl_state->finish;
}

/* -------------------------------------------------------------------------- */
STATIC void history_save()
{
#ifdef RL_HISTORY_FILE
	char buf[RL_MAX_LENGTH*2], *in = buf;
	int fd = open(RL_HISTORY_FILE, O_CREAT|O_WRONLY|O_TRUNC, 0644);
	if (fd < 0)
		return;

	rl_history_t *h = &rl_state->history;
	int c = h->size;
	char **pos = h->lines;
	while (c--) {
		if (write(fd, pos[0], strlen(pos[0])) < 0 || write(fd, "\n", 1) < 0)
			break;
		++pos;
	}

	close(fd);
#endif
}

/* -------------------------------------------------------------------------- */
STATIC void history_empty()
{
	rl_history_t *h = &rl_state->history;
	int c = h->size;
	char **pos = h->lines;
	while (c--) {
		free(*pos);
		*pos++ = NULL;
	}
	h->size = 0;
}

/* -------------------------------------------------------------------------- */
STATIC void history_add(char const *string)
{
	rl_history_t *h = &rl_state->history;

	free(h->line); h->line = NULL;
	if (!string[0])
		return;
	if (h->size && !strcmp(h->lines[h->size-1], string)) {
		h->current = h->size;
		return;
	}

	if (h->size >= countof(h->lines)) {
		free(h->lines[0]);
		memmove(h->lines, h->lines+1, sizeof(h->lines[0])*--(h->size));
	}
	h->lines[h->size++] = strdup(string);
	h->current = h->size;
}

/* -------------------------------------------------------------------------- */
STATIC void history_restore()
{
#ifdef RL_HISTORY_FILE
	int fd = open(RL_HISTORY_FILE, O_RDONLY);
	if (fd < 0)
		return;

	history_empty();

	char line[RL_MAX_LENGTH*2 + 4];
	char *in = line, *iend = line + sizeof(line) - 1;
	int count;

	while ((count = safe_read(fd, in, iend - in)) > 0) {
		char *eoln, *end = in + count;
		for (eoln = in; eoln < end; ++eoln)
			if (*eoln == '\n') {
				*eoln = 0;
				history_add(in);
				in = eoln + 1;
			}
		if (in < end)
			memmove(line, in, iend - in);
		in = line + (iend - in);
	}

	close(fd);
#endif
}

/* -------------------------------------------------------------------------- */
static void rl_update_window()
{
	if (!rl_window_check())
		return;

	int cur_pos = rl_state->cur_pos;
	int cols = rl_window.cols;
	rlc_cursor_home();
	rl_out_purge();
	rl_window_update();
	int tail = (1 + rl_window.cols - cols) * ((rl_state->prompt_width + rl_state->length) / cols);
	rl_redraw(1, tail >= 0 ? tail : 0);
	rl_move(cur_pos);
	rl_state->cur_pos = cur_pos;
	rl_out_purge();
}

/* -------------------------------------------------------------------------- */
void readline_init(rl_get_completion_fn *gc)
{
	if (rl_state)
		readline_free();
	rl_state = (rl_state_t *)malloc(sizeof(rl_state_t));
	memset(rl_state, 0, sizeof(*rl_state));
	history_restore();
	rl_state->_get_completion = gc;
	rl_window_init();
}

/* -------------------------------------------------------------------------- */
void readline_free()
{
	if (!rl_state)
		return;

	rl_window_free();
	history_save();
	history_empty();
	free(rl_state);
	rl_state = NULL;
}

#ifdef RL_SORT_HINTS
static int rl_strscmp(void const *l, void const *r)
{
	return strcmp(*(char const *const*)l, *(char const *const*)r);
}
#endif

/* -------------------------------------------------------------------------- */
void rl_dump_options(char const * const *options)
{
	if (!*options) /* empty list */
		return;

	char const * const *opt = options;
	int col_width = 0, width;
	while (*opt)
		if ((width = strlen(*opt++)) > col_width)
			col_width = width;

#ifdef RL_SORT_HINTS
	qsort((void *)options, opt - options, sizeof(char *), rl_strscmp);
#endif

	col_width += 2;
	int cols = rl_window.cols / col_width;
	if (!cols)
		cols = 1;

	int cur_pos = rl_state->cur_pos;
	rlc_cursor_end();
	rl_printf("\r\n");

	opt = options;
	while (*opt) {
		int c = cols;
		do {
			rl_printf("%-*s", col_width, *opt++);
		} while (*opt && --c);
		rl_printf("\r\n");
	}
	rl_state->cur_pos = cur_pos;
	rl_redraw(0, 0); /* not in place */
}

/* -------------------------------------------------------------------------- */
void rl_dump_hint(char const *fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	char outbuf[4096];
	int length = vsnprintf(outbuf, sizeof(outbuf), fmt, va);
	va_end(va);

	int cur_pos = rl_state->cur_pos;
	rlc_cursor_end();
	rl_printf("\n\r");

	rl_out(outbuf, length);
	rl_printf("\n\r");
	rl_state->cur_pos = cur_pos;
	rl_redraw(0, 0); /* not in place */
}

/* -------------------------------------------------------------------------- */
char *readline(char const *prompt, char const *string)
{
	if (!isatty(STDIN_FILENO)) {
		if (fgets(rl_state->raw, sizeof(rl_state->raw), stdin) == NULL)
			return NULL;
		char *end = rl_state->raw + strlen(rl_state->raw) - 1;
		while (*end == '\r' || *end == '\n')
			--end;
		*++end = 0;
		return rl_state->raw;
	}

	rl_state->raw[0] = 0;
	rl_state->line[0] = 0;
	rl_state->length = rl_state->cur_pos = rl_state->finish = 0;
	rl_state->prompt = prompt;
	rl_state->prompt_width = utf8_width(prompt);

	rl_term_raw();
//	char const *term = getenv("TERM");
//	rl_printf("width=%u; term=%s\n\r%s", rl_window.cols, term ?: "unknown", SET_WRAP_MODE);
	rl_printf("%s", prompt);

	if (string)
		rl_set_text(string, 1);

	rl_out_purge();

	int rdn;
	char ch, seq[12], *seqpos = seq;
	while (safe_read(STDIN_FILENO, &ch, 1) > 0) {
		if (seqpos >= seq + sizeof(seq))
			seqpos = seq; /* wrong sequence -- wrong reaction :) */
		*seqpos++ = ch;
		*seqpos = 0;
		if (!skip_char_seq(seq))
			continue; /* wrong seq */

		if (rl_exec_seq(seq))
			break; /* finish */

		seqpos = seq;
	}

	rlc_cursor_end();
	gtoutf8(rl_state->raw, rl_state->line, -1);
	
	rl_term_unraw();
	history_add(rl_state->raw);
	rl_printf("\n");
	rl_out_purge();
	return rl_state->raw;
}

#ifdef RL_TEST
/* -------------------------------------------------------------------------- */
char *readline_test(char const *prompt, char const *string)
{
	if (!isatty(STDIN_FILENO)) {
		if (fgets(rl_state->raw, sizeof(rl_state->raw), stdin) == NULL)
			return NULL;
		char *end = rl_state->raw + strlen(rl_state->raw) - 1;
		while (*end == '\r' || *end == '\n')
			--end;
		*++end = 0;
		return rl_state->raw;
	}

	rl_state->raw[0] = 0;
	rl_state->line[0] = 0;
	rl_state->length = rl_state->cur_pos = rl_state->finish = 0;

	rl_term_raw();
	rl_printf("%s%s", SET_WRAP_MODE, prompt);

	if (string)
		rl_set_text(string, 1);

	int rdn;
	char ch, seq[12], *seqpos = seq;
	while ((rdn = read(STDIN_FILENO, &ch, 1)) > 0) {
		if (seqpos >= seq + sizeof(seq))
			seqpos = seq; /* wrong sequence -- wrong reaction :) */
		*seqpos++ = ch;
		*seqpos = 0;
		if (!skip_char_seq(seq))
			continue; /* wrong seq */

/*		if (rl_exec_seq(seq))
			break; /* finish */

		seqpos = seq;

		char str[128], *out = str;
		out += snprintf(out, sizeof(str) - (out-str), "\n\r", rdn);
		char *in = seq;
		while (*in) {
			char ch = *in++;
			out += snprintf(out , sizeof(str) - (out - str), !(ch & -32) ? "\\%03o" : "%c", ch);
		}

		out += snprintf(out, sizeof(str) - (out-str), " : ");
		in = seq;
		while (*in) {
			char ch = *in++;
			out += snprintf(out , sizeof(str) - (out - str), "%02X", 255 & (unsigned int)ch);
		}

		out += snprintf(out, sizeof(str) - (out-str), " : ");
		rl_glyph_t ucs[16], *uc = ucs;
		rl_glyph_t *ucend = utf8tog(ucs, seq);
		while (uc < ucend) {
			out += snprintf(out, sizeof(str) - (out-str), "%04X ", *uc++);
		}

		char raw[16];
		char *rend = gtoutf8(raw, ucs, -1);

		out += snprintf(out, sizeof(str) - (out-str), " : ");
		in = raw;
		while (*in) {
			char ch = *in++;
			out += snprintf(out , sizeof(str) - (out - str), "%02X", 255 & (unsigned int)ch);
		}
		rl_out(str, out - str);//*/
		if (seq[0] == 3)
			break;
	}

	gtoutf8(rl_state->raw, rl_state->line, -1);
	
	rl_term_unraw();
	history_add(rl_state->raw);
	return rl_state->raw;
}
#endif

