/*
 * 		    GNU GENERAL PUBLIC LICENSE
 *		       Version 2, June 1991
 * 
 * Copyright (C) 2005-2021 Git Project
 * Copyright (C) 2021 Matheus Tavares
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses
 *
 * The code in this file was originally copied from the Git project[1] (files
 * cache.h and git-compat-utils.h), at commit 88d915a634b44 ("A few fixes
 * before -rc2", 2021-11-04), and modified to be integrated with this project
 * and remove dependencies with other non-ported Git functions.
 * [1]: https://github.com/git/git
 */

#ifndef _ARRAY_H
#define _ARRAY_H

#include <stdint.h>
#include <limits.h>
#include "error.h"
#include "wrappers.h"

/*
 * FREE_AND_NULL(ptr) is like free(ptr) followed by ptr = NULL. Note
 * that ptr is used twice, so don't pass e.g. ptr++.
 */
#define FREE_AND_NULL(p) do { free(p); (p) = NULL; } while (0)


#define bitsizeof(x)  (CHAR_BIT * sizeof(x))
#define maximum_unsigned_value_of_type(a) \
    (UINTMAX_MAX >> (bitsizeof(uintmax_t) - bitsizeof(a)))
/*
 * Returns true if the multiplication of "a" and "b" will
 * overflow. The types of "a" and "b" must match and must be unsigned.
 * Note that this macro evaluates "a" twice!
 */
#define unsigned_mult_overflows(a, b) \
    ((a) && (b) > maximum_unsigned_value_of_type(a) / (a))

static inline size_t st_mult(size_t a, size_t b)
{
	if (unsigned_mult_overflows(a, b))
		die("size_t overflow: %zu * %zu", a, b);
	return a * b;
}

#define ALLOC_ARRAY(x, alloc) (x) = xmalloc(st_mult(sizeof(*(x)), (alloc)))
#define CALLOC_ARRAY(x, alloc) (x) = xcalloc((alloc), sizeof(*(x)))
#define REALLOC_ARRAY(x, alloc) (x) = xrealloc((x), st_mult(sizeof(*(x)), (alloc)))

#define alloc_nr(x) (((x)+16)*3/2)

/**
 * Dynamically growing an array using realloc() is error prone and boring.
 *
 * Define your array with:
 *
 * - a pointer (`item`) that points at the array, initialized to `NULL`
 *   (although please name the variable based on its contents, not on its
 *   type);
 *
 * - an integer variable (`alloc`) that keeps track of how big the current
 *   allocation is, initialized to `0`;
 *
 * - another integer variable (`nr`) to keep track of how many elements the
 *   array currently has, initialized to `0`.
 *
 * Then before adding `n`th element to the item, call `ALLOC_GROW(item, n,
 * alloc)`.  This ensures that the array can hold at least `n` elements by
 * calling `realloc(3)` and adjusting `alloc` variable.
 *
 * ------------
 * sometype *item;
 * size_t nr;
 * size_t alloc
 *
 * for (i = 0; i < nr; i++)
 * 	if (we like item[i] already)
 * 		return;
 *
 * // we did not like any existing one, so add one
 * ALLOC_GROW(item, nr + 1, alloc);
 * item[nr++] = value you like;
 * ------------
 *
 * You are responsible for updating the `nr` variable.
 *
 * If you need to specify the number of elements to allocate explicitly
 * then use the macro `REALLOC_ARRAY(item, alloc)` instead of `ALLOC_GROW`.
 *
 * DO NOT USE any expression with side-effect for 'x', 'nr', or 'alloc'.
 */
#define ALLOC_GROW(x, nr, alloc) \
	do { \
		if ((nr) > alloc) { \
			if (alloc_nr(alloc) < (nr)) \
				alloc = (nr); \
			else \
				alloc = alloc_nr(alloc); \
			REALLOC_ARRAY(x, alloc); \
		} \
	} while (0)


#define ARRAY(type) \
	struct { \
		type *arr; \
		size_t nr, alloc; \
	}

#define ARRAY_APPEND(array, val) \
{ \
	ALLOC_GROW((array)->arr, (array)->nr + 1, (array)->alloc); \
	(array)->arr[(array)->nr++] = (val); \
} while (0)

#define FREE_ARRAY(array) \
{ \
	free((array)->arr); \
	(array)->nr = (array)->alloc = 0; \
} while (0)

/* Quite inefficient, but that's fine for our needs. */
#define ARRAY_REMOVE(array, val) \
{ \
	for (size_t i = 0; i < (array)->nr; i++) { \
		if ((array)->arr[i] == (val)) { \
			size_t to_move = ((array)->nr - i - 1) * sizeof((array)->arr[0]); \
			if (to_move) \
				memmove(&((array)->arr[i]), &((array)->arr[i+1]), to_move); \
			(array)->nr--; \
			if ((array)->nr && (array)->nr / (double)(array)->alloc < 0.25) { \
				(array)->alloc /= 2; \
				if ((array)->alloc < (array)->nr) \
					(array)->alloc = (array)->nr + 8; \
				REALLOC_ARRAY((array)->arr, (array)->alloc); \
			} \
			break; \
		} \
	} \
} while (0)

#define ARRAY_INIT(array) memset(array, 0, sizeof(*(array)))

#endif
