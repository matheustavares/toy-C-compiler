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
 * Wrappers originally coppied from the Git project[1], file wrapper.c, at
 * commit 88d915a634b44 ("A few fixes  before -rc2", 2021-11-04). The
 * routines were modified to remove dependencies and simplify/tailor the
 * behavior to our specific needs.
 * [1]: https://github.com/git/git
 */

#ifndef _WRAPPERS_H
#define _WRAPPERS_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "error.h"

static void *xmalloc(size_t nr)
{
	void *ptr = malloc(nr);
	if (!ptr)
		die("malloc failed");
	return ptr;
}

static void *xcalloc(size_t nmemb, size_t size)
{
	void *ptr = calloc(nmemb, size);
	if (!ptr)
		die("malloc failed");
	return ptr;
}

static void *xrealloc(void *ptr, size_t nr)
{
	ptr = realloc(ptr, nr);
	if (!ptr)
		die("realloc failed");
	return ptr;
}

static int xsnprintf(char *dst, size_t max, const char *fmt, ...)
{
	va_list ap;
	int len;

	va_start(ap, fmt);
	len = vsnprintf(dst, max, fmt, ap);
	va_end(ap);

	if (len < 0)
		die_errno("vsnprintf failed");
	if (len >= max)
		die("attempt to vsnprintf into too-small buffer");
	return len;
}

static char *xstrdup(const char *str)
{
	char *ret = strdup(str);
	if (!ret)
		die_errno("strdup failed");
	return ret;
}

#endif
