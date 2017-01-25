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
#include <string.h>
#include <termios.h>
#include "readline.h"

#ifndef countof
# define countof(arr)  (sizeof(arr)/sizeof(arr[0]))
#endif

/* -------------------------------------------------------------------------- */
typedef
struct _ac_ {
	char const *name;
	struct _ac_ const *const sub;
} ac_item_t;

/* -------------------------------------------------------------------------- */
static const ac_item_t ac_flash[] = {
	{ "set" },
	{ "get" },
	{ "clear" },
	{ "cat" },
	{ "all" },
	{ NULL }
};

static const ac_item_t ac_system[] = {
	{ "status" },
	{ "diag" },
	{ "mode" },
	{ "upgrade" },
	{ NULL }
};

static const ac_item_t ac_wan[] = {
	{ "ppp_pppoe" },
	{ "ppp_ptpt" },
	{ "ppp_l2tp" },
	{ "ip" },
	{ NULL }
};

static const ac_item_t ac_root[] = {
	{ "flash", ac_flash },
	{ "system", ac_system },
	{ "wan", ac_wan },
	{ "exit" },
	{ NULL }
};

/* -------------------------------------------------------------------------- */
char const *rl_get_completion(char const *start, char const *cur_pos)
{
	ac_item_t const *list = ac_root;
	ac_item_t const *cur;

	char const *tok_start, *tok_end = start;
	do {
		tok_start = tok_end;
		while (*tok_end && *tok_end != ' ')
			++tok_end;

		if (cur_pos <= tok_end) {
			int tok_len = cur_pos - tok_start;
			char const *options[64];
			char const **opt = options, **oend = options + countof(options) - 1;
			char const *common = NULL;
			int com_len = 0;

			for (cur = list; cur->name && opt < oend; ++cur)
				if (!tok_len || !memcmp(cur->name, tok_start, tok_len)) {
					char const *sample = (*opt++ = cur->name) + tok_len;
					if (!common) {
						common = sample;
						com_len = strlen(sample);
					} else {
						int pos = 0;
						while (pos < com_len && sample[pos] == common[pos])
							++pos;
						com_len = pos;
					}
				}
			*opt = NULL;

			if (opt - options == 1 || com_len) {
				static char out[64];
				if (com_len > sizeof(out) - 2)
					com_len = sizeof(out) - 2;
				if (com_len)
					memcpy(out, common, com_len);
				if (opt - options == 1) {
					out[com_len] = ' ';
					out[com_len + 1] = 0;
				} else
					out[com_len] = 0;
				return out;
			}

			/* print all options */
			rl_dump_options(options);
			return NULL;
		}

		int tok_len = tok_end - tok_start;
		for (cur = list; cur->name; ++cur)
			if (!memcmp(cur->name, tok_start, tok_len) && !cur->name[tok_len]) {
				list = cur->sub;
				goto _sub;
			}
		return NULL;
_sub:
		while (*tok_end == ' ')
			++tok_end;
	} while (list);

	return NULL;//"tootoo!";
}


int is_cmd(char const *line, char const *cmd) {
	char const *end = line;
	for (; *end && *end != ' '; ++end);
	for (; line < end && *cmd; ++line, ++cmd)
		if (*line != *cmd)
			return 0;
	return !*cmd;
}


/* -------------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
	readline_init(rl_get_completion);

	char *line;
	do {
		line = readline("ogo>", argc > 1 ? argv[1] : NULL);
		printf("exec '%s'\n", line);
#ifdef RL_TEST
		if (!strcmp(line, "test"))
			readline_test("ogo>", argc > 1 ? argv[1] : NULL);
#endif
	} while (line && !is_cmd(line, "exit"));

	readline_free();
	return 0;
}
