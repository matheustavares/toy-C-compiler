#ifndef _VAR_MAP_H
#define _VAR_MAP_H

/*
 * A variable mapping, associates variable names (strings) with offsets
 * in which they are stored in the stack (relative to %rpb). Note that the
 * offsets are stored in absolute value, the caller is responsible for
 * subtracting them from %rpb.
 *
 * The API does not take responsability for the strings' memory. That is, it
 * does not strdup() nor free() them.
 */

struct hsearch_data;

struct var_map {
	struct hsearch_data *table;
	const char **keys;
	size_t nr, table_alloc, keys_alloc;
};

#define INITIAL_TABLE_ALLOC 20
void var_map_init_size(struct var_map *map, size_t size);
#define var_map_init(map) var_map_init_size(map, INITIAL_TABLE_ALLOC)

void var_map_cpy(struct var_map *dst, struct var_map *src);
void var_map_destroy(struct var_map *map);

/* Return -1 if not found. */
ssize_t var_map_find(struct var_map *map, const char *var);
int var_map_has(struct var_map *map, const char *var);

/* If var was already in map, overwrite its payload. */
void var_map_put(struct var_map *map, const char *var, size_t off);

typedef int (*var_map_iterator_fn)(const char *key, ssize_t val, void *data);
void var_map_iterate(struct var_map *map, var_map_iterator_fn fn, void *udata);

#endif
