#ifndef _STRMAP_H
#define _STRMAP_H

/*
 * A generic hashtable with strings as keys.
 *
 * struct strmap MUST be memset to zero before initialization and/or copying
 * (only the destination struct). strmap_destroy() leaves the struct in a
 * state ready for reinitialization.
 *
 * Note: memory for both the keys and the values are responsibility of
 * the user. We will neither copy them nor free. Make sure the data
 * stays valid while using the strmap API and do not forget to free it
 * later.
 */

struct hsearch_data;
typedef void (*strmap_val_cpy_fn)(void **dst, void **src);

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

/* Returns 1 and saves value in val iff str is in map. */
int strmap_find(struct strmap *map, const char *str, void **val);
static inline int strmap_has(struct strmap *map, const char *key)
{
	return strmap_find(map, key, NULL);
}

/* Return the previous value or NULL. */
void *strmap_put(struct strmap *map, const char *str, void *val);

typedef int (*strmap_iter_callback_fn)(const char *key, void *val, void *udata);
void strmap_iterate(struct strmap *map, strmap_iter_callback_fn fn, void *udata);

#endif
