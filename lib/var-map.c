#define _GNU_SOURCE /* hsearch: *_r variants */ 
#include <search.h>
#include <assert.h>
#include "error.h"
#include "wrappers.h"
#include "array.h"
#include "var-map.h"

void var_map_init_size(struct var_map *map, size_t size)
{
	if (map->table)
		die("BUG: called var_map_init with already initialized table");
	map->table = xcalloc(1, sizeof(*(map->table)));
	map->nr = 0;
	map->keys = NULL;
	map->table_alloc = size;
	if (!hcreate_r(map->table_alloc, map->table))
		die_errno("hcreate_r error");
}

static void copy_hsearch_data(struct hsearch_data *src, struct hsearch_data *dst,
			      const char **keys, size_t nr)
{
	for (size_t i = 0; i < nr; i++) {
		ENTRY search, *found;
		search.key = (char *)keys[i];
		if (!hsearch_r(search, FIND, &found, src))
			die_errno("BUG: key '%s' should be in var_map but it is not", keys[i]);
		search.data = found->data;
		if (!hsearch_r(search, ENTER, &found, dst))
			die_errno("BUG: hsearch_r fail to enter key on copy_hsearch_data");
	}
}

void var_map_cpy(struct var_map *dst, struct var_map *src)
{
	if (!src->table || dst->table)
		die("BUG: var_man_cpy needs initialized src and uninitialized dst");
	var_map_init_size(dst, src->table_alloc);
	copy_hsearch_data(src->table, dst->table, src->keys, src->nr);
	dst->nr = src->nr;
	dst->keys_alloc = src->keys_alloc;
	dst->keys = xmalloc(dst->keys_alloc * sizeof(*(dst->keys)));
	for (size_t i = 0; i < dst->nr; i++)
		dst->keys[i] = src->keys[i];
}

ssize_t var_map_find(struct var_map *map, const char *var)
{
	ENTRY search, *found;
	search.key = (char *)var;
	if (!map->table)
		die("BUG: var_map_find called with uninitialized map");
	if (!hsearch_r(search, FIND, &found, map->table))
		return -1;
	return (ssize_t)found->data;
}

void var_map_put(struct var_map *map, const char *var, size_t off)
{
	ENTRY search, *found;
	search.key = (char *)var;
	if (!map->table)
		die("BUG: var_map_put called with uninitialized map");
	if (hsearch_r(search, FIND, &found, map->table)) {
		found->data = (void *)off;
	} else {
		/* key not found */
		if (map->nr + 1 > map->table_alloc) {
			/* resize */
			size_t new_size = map->table_alloc * 2;
			struct hsearch_data *table = xcalloc(1, sizeof(*table));
			if (!hcreate_r(new_size, table))
				die_errno("hcreate_r error when resizing table");
			copy_hsearch_data(map->table, table, map->keys, map->nr);
			hdestroy_r(map->table);
			free(map->table);
			map->table = table;
			map->table_alloc = new_size;
		}
		search.data = (void *)off;
		if (!hsearch_r(search, ENTER, &found, map->table))
			die_errno("BUG? hsearsh_r failed after resize (%zu entries out of %zu slots)",
				  map->nr, map->table_alloc);
		ALLOC_GROW(map->keys, map->nr + 1, map->keys_alloc);
		map->keys[map->nr] = var;
		map->nr++;
	}
}
	
int var_map_has(struct var_map *map, const char *var)
{
	return var_map_find(map, var) >= 0;
}

void var_map_iterate(struct var_map *map, var_map_iterator_fn fn, void *udata)
{
	if (!map->table)
		die("BUG: var_map_iterate called with uninitialized map");
	for (size_t i = 0; i < map->nr; i++) {
		ENTRY search, *found;
		search.key = (char *)map->keys[i];
		if (!hsearch_r(search, FIND, &found, map->table))
			die_errno("BUG: key '%s' should be in var_map but it is not", map->keys[i]);
		if (fn(map->keys[i], (ssize_t)found->data, udata))
			break;
	}
}

void var_map_destroy(struct var_map *map)
{
	if (!map->table)
		die("BUG: var_map_destroy called with uninitialized map");
	hdestroy_r(map->table);
	FREE_AND_NULL(map->table);
	FREE_AND_NULL(map->keys);
}
