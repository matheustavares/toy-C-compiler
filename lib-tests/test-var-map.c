#include <stdio.h>
#include <stdlib.h>
#include "../util.h"
#include "../lib/array.h"
#include "../lib/var-map.h"

int print_var_map_entry(const char *key, ssize_t val, void *_)
{
	printf(" %s -> %zd\n", key, val);
	return 0;
}

int main(int argc, char **argv)
{
	struct var_map vmap, vmap2;
	struct var_map *map_ptr = &vmap, *other = &vmap2;
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
			var_map_init(map_ptr);
			printf("init\n");
		} else if (skip_prefix(*argv, "init=", &val)) {
			var_map_init_size(map_ptr, atoi(val));
			printf("init %s\n", val);
		} else if (!strcmp(*argv, "copy")) {
			struct var_map *aux;
			var_map_cpy(other, map_ptr);
			var_map_destroy(map_ptr);
			aux = map_ptr;
			map_ptr = other;
			other = aux;
			printf("copy\n");
		} else if (!strcmp(*argv, "destroy")) {
			var_map_destroy(map_ptr);
			printf("destroy\n");
		} else if (skip_prefix(*argv, "find=", &val)) {
			printf("find '%s': %zd\n", val, var_map_find(map_ptr, val));
		} else if (skip_prefix(*argv, "has=", &val)) {
			printf("has '%s': %d\n", val, var_map_has(map_ptr, val));
		} else if (skip_prefix(*argv, "put=", &val)) {
			const char *comma;
			int size;
			for (comma = val; *comma && *comma != ','; comma++)
				;
			if (!*comma || !*(++comma))
				die("unknown option '%s'", *argv);
			size = atoi(comma);

			ALLOC_GROW(to_free, nr + 1, alloc);
			val = to_free[nr++] = xstrndup(val, comma - val - 1);

			printf("put: '%s' -> %d\n", val, size);
			var_map_put(map_ptr, val, size);
		} else if (!strcmp(*argv, "list")) {
			printf("list\n");
			var_map_iterate(map_ptr, print_var_map_entry, NULL);
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
