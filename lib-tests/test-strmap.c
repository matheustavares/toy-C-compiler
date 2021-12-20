#include <stdio.h>
#include <stdlib.h>
#include "../util.h"
#include "../lib/array.h"
#include "../lib/strmap.h"

static int print_strmap_entry(const char *key, void *val, void *_)
{
	int ival = (intmax_t)val;
	printf(" %s -> %d\n", key, ival);
	if (!strcmp(key, "break"))
		return 1;
	return 0;
}

static void strmap_addr_copy(void **ptr_a, void **ptr_b)
{
	*ptr_b = *ptr_a;
}

int main(int argc, char **argv)
{
	struct strmap map = { 0 }, map2 = { 0 };
	struct strmap *map_ptr = &map, *other = &map2;
	const char *val;

	char **to_free = NULL;
	size_t nr = 0, alloc = 0;

	for (argv++; *argv; argv++) {
		if (!strcmp(*argv, "-h") || !strcmp(*argv, "--help")) {
			printf("Options:\n");
			printf("    init\n");
			printf("    init=<off>\n");
			printf("    copy\n");
			printf("    destroy\n");
			printf("    find=<str>\n");
			printf("    has=<str>\n");
			printf("    put=<str>,<off>\n");
			printf("    list\n");
			printf("    info\n");
			return 0;
		} else if (!strcmp(*argv, "init")) {
			strmap_init(map_ptr, strmap_addr_copy);
			printf("init\n");
		} else if (skip_prefix(*argv, "init=", &val)) {
			strmap_init_size(map_ptr, strmap_addr_copy, atoi(val));
			printf("init %s\n", val);
		} else if (!strcmp(*argv, "copy")) {
			struct strmap *aux;
			strmap_cpy(other, map_ptr);
			strmap_destroy(map_ptr);
			aux = map_ptr;
			map_ptr = other;
			other = aux;
			printf("copy\n");
		} else if (!strcmp(*argv, "destroy")) {
			strmap_destroy(map_ptr);
			printf("destroy\n");
		} else if (skip_prefix(*argv, "find=", &val)) {
			printf("find '%s': %d\n", val, (int)(intmax_t)strmap_find(map_ptr, val));
		} else if (skip_prefix(*argv, "has=", &val)) {
			printf("has '%s': %d\n", val, strmap_has(map_ptr, val));
		} else if (skip_prefix(*argv, "put=", &val)) {
			const char *comma;
			int map_val;
			for (comma = val; *comma && *comma != ','; comma++)
				;
			if (!*comma || !*(++comma))
				die("unknown option '%s'", *argv);
			map_val = atoi(comma);

			ALLOC_GROW(to_free, nr + 1, alloc);
			val = to_free[nr++] = xstrndup(val, comma - val - 1);

			printf("put: '%s' -> %d\n", val, map_val);
			strmap_put(map_ptr, val, (void *)(intmax_t)map_val);
		} else if (!strcmp(*argv, "list")) {
			printf("list\n");
			strmap_iterate(map_ptr, print_strmap_entry, NULL);
		} else if (!strcmp(*argv, "info")) {
			printf("info:\n");
			printf("  nr:          %zu\n", map_ptr->nr);
			printf("  table_alloc: %zu\n", map_ptr->table_alloc);
			printf("  keys_alloc:  %zu\n", map_ptr->keys_alloc);
		} else {
			die("unknown option '%s'", *argv);
		}
	}

	for (size_t i = 0; i < nr; i++)
		free(to_free[i]);
	free(to_free);

	return 0;
}
