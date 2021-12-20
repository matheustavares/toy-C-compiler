#ifndef _STRMAP_H
#define _STRMAP_H

/*
 * A generic hashtable with strings as keys.
 *
 * Note: memory for both the keys and the values are responsibility of
 * the user. We will neither copy them nor free. Make sure the data
 * stays valid while using the strmap API and do not forget to free it
 * later.
 */

struct hsearch_data;
typedef void (*strmap_val_cpy_fn)(void **a, void **b);

struct strmap {
	struct hsearch_data *table;
	const char **keys;
	size_t nr, table_alloc, keys_alloc;
	strmap_val_cpy_fn val_cpy_fn;
};

#define INITIAL_TABLE_ALLOC 20
void strmap_init_size(struct strmap *map, strmap_val_cpy_fn val_cpy_fn,
		      size_t size);
#define strmap_init(map, val_cpy_fn) \
	strmap_init_size(map, val_cpy_fn, INITIAL_TABLE_ALLOC)

void strmap_cpy(struct strmap *dst, struct strmap *src);
void strmap_destroy(struct strmap *map);

void *strmap_find(struct strmap *map, const char *str);
static inline int strmap_has(struct strmap *map, const char *val)
{
	return !!strmap_find(map, val);
}

/* Return the previous value or NULL. */
void *strmap_put(struct strmap *map, const char *str, void *val);

typedef int (*strmap_iter_callback_fn)(const char *key, void *val, void *udata);
void strmap_iterate(struct strmap *map, strmap_iter_callback_fn fn, void *udata);

#endif
