#define _GNU_SOURCE /* hsearch: *_r variants */ 
#include <search.h>
#include <assert.h>
#include "error.h"
#include "wrappers.h"
#include "array.h"
#include "strmap.h"

void strmap_init_size(struct strmap *map, strmap_val_cpy_fn val_cpy_fn, size_t size)
{
	if (map->table)
		die("BUG: called strmap_init with already initialized table");
	map->table = xcalloc(1, sizeof(*(map->table)));
	map->nr = 0;
	map->keys = NULL;
	map->table_alloc = size;
	map->val_cpy_fn = val_cpy_fn;
	if (!hcreate_r(map->table_alloc, map->table))
		die_errno("hcreate_r error");
}

static void copy_hsearch_data(struct hsearch_data *src, struct hsearch_data *dst,
			      const char **keys, size_t nr,
			      strmap_val_cpy_fn cpy_fn)
{
	for (size_t i = 0; i < nr; i++) {
		ENTRY search, *found;
		search.key = (char *)keys[i];
		if (!hsearch_r(search, FIND, &found, src))
			die_errno("BUG: key '%s' should be in hsearch map but it is not", keys[i]);
		cpy_fn(&found->data, &search.data);
		if (!hsearch_r(search, ENTER, &found, dst))
			die_errno("BUG: hsearch_r fail to enter key on copy_hsearch_data");
	}
}

void strmap_cpy(struct strmap *dst, struct strmap *src)
{
	if (!src->table || dst->table)
		die("BUG: strman_cpy needs initialized src and uninitialized dst");
	strmap_init_size(dst, src->val_cpy_fn, src->table_alloc);
	copy_hsearch_data(src->table, dst->table, src->keys, src->nr, src->val_cpy_fn);
	dst->nr = src->nr;
	dst->keys_alloc = src->keys_alloc;
	dst->keys = xmalloc(dst->keys_alloc * sizeof(*(dst->keys)));
	for (size_t i = 0; i < dst->nr; i++)
		dst->keys[i] = src->keys[i];
}

void *strmap_find(struct strmap *map, const char *str)
{
	ENTRY search, *found;
	search.key = (char *)str;
	if (!map->table)
		die("BUG: strmap_find called with uninitialized map");
	if (!hsearch_r(search, FIND, &found, map->table))
		return NULL;
	return found->data;
}

void *strmap_put(struct strmap *map, const char *str, void *val)
{
	ENTRY search, *found;
	search.key = (char *)str;
	void *old = NULL;
	if (!map->table)
		die("BUG: strmap_put called with uninitialized map");
	if (hsearch_r(search, FIND, &found, map->table)) {
		old = found->data;
		found->data = val;
	} else {
		/* key not found */
		if (map->nr + 1 > map->table_alloc) {
			/* resize */
			size_t new_size = map->table_alloc * 2;
			struct hsearch_data *table = xcalloc(1, sizeof(*table));
			if (!hcreate_r(new_size, table))
				die_errno("hcreate_r error when resizing table");
			copy_hsearch_data(map->table, table, map->keys, map->nr,
					  map->val_cpy_fn);
			hdestroy_r(map->table);
			free(map->table);
			map->table = table;
			map->table_alloc = new_size;
		}
		search.data = val;
		if (!hsearch_r(search, ENTER, &found, map->table))
			die_errno("BUG? hsearsh_r failed after resize (%zu entries out of %zu slots)",
				  map->nr, map->table_alloc);
		ALLOC_GROW(map->keys, map->nr + 1, map->keys_alloc);
		map->keys[map->nr] = str;
		map->nr++;
	}
	return old;
}

void strmap_iterate(struct strmap *map, strmap_iter_callback_fn fn, void *udata)
{
	if (!map->table)
		die("BUG: strmap_iterate called with uninitialized map");
	for (size_t i = 0; i < map->nr; i++) {
		ENTRY search, *found;
		search.key = (char *)map->keys[i];
		if (!hsearch_r(search, FIND, &found, map->table))
			die_errno("BUG: key '%s' should be in hsearch table but it is not", map->keys[i]);
		if (fn(map->keys[i], found->data, udata))
			break;
	}
}

void strmap_destroy(struct strmap *map)
{
	if (!map->table)
		die("BUG: strmap_destroy called with uninitialized map");
	hdestroy_r(map->table);
	FREE_AND_NULL(map->table);
	FREE_AND_NULL(map->keys);
}
