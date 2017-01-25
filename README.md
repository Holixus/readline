# readline
A small readline function for embedded devices without dependencies.

* UTF-8 compiant
* Completion
* VT-100, VT-52, PuTTY compatible
* History story file
* 1200 lines of source code


## Build & Install

```sh
git clone git@github.com:Holixus/readline.git readline
cd readline
cmake .
make
make install
```

For interactive build configure use `ccmake .`


## Configure options

```cmake
OPTION(BUILD_EXAMPLES     "build examples" OFF)

set(RL_MAX_LENGTH      "1024"  CACHE STRING "maximum length of input line")
set(RL_HISTORY_HEIGHT  "32"    CACHE STRING "Height of history file")
set(RL_HISTORY_FILE    "/tmp/.rl_history" CACHE FILEPATH "Default history file")
set(RL_WINDOW_WIDTH    "80"    CACHE STRING "Default window width")
set(RL_SORT_HINTS      ON      CACHE BOOL "Sort <tab> hints list")
set(RL_USE_WRITE       ON      CACHE BOOL "Use write() instead of fwrite()")
```


## API

### readline_init
```c
typedef char const *(rl_get_completion_fn)(char const *start, char const *cur_pos);

void readline_init(rl_get_completion_fn *gc);
```

### readline free
```c
void readline_free();
```

### readline_history_load
```c
void readline_history_load(char const *file);
```

### readline
```c
char *readline(char const *prompt, char const *init);
```

### Completion

```c
void rl_dump_options(char const * const *options);
void rl_dump_hint(char const *fmt, ...);
```


### Example

```c
int main(int argc, char *argv[])
{
	readline_init(NULL);
	readline_history_load("./.history");

	char *line;
	do {
		line = readline("ogo>", NULL);
		printf("exec '%s'\n", line);
	} while (line && *line); // exit on empty line

	readline_free();
	return 0;
}
```
