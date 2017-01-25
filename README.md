# readline
A small readline function for embedded devices.

* UTF-8 compiant
* Completion
* VT-100, VT-52, PuTTY compatible
* History story file
* 1200 lines of source code


## Build


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

	char *line;
	do {
		line = readline("ogo>", NULL);
		printf("exec '%s'\n", line);
	} while (line && !is_cmd(line, "exit"));

	readline_free();
	return 0;
}
```
