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

#ifndef READLINE_H_
#define READLINE_H_

/* -------------------------------------------------------------------------- */
typedef
char const *(rl_get_completion_fn)(char const *start, char const *cur_pos);

void rl_dump_options(char const * const *options);
void rl_dump_hint(char const *fmt, ...);

void readline_init(rl_get_completion_fn *gc);
void readline_free();

void readline_history_load(char const *file);

char *readline(char const *prompt, char const *string);
#ifdef RL_TEST
char *readline_test(char const *prompt, char const *string);
#endif

#endif /* READLINE_H_ */
