/*
 * 		    GNU GENERAL PUBLIC LICENSE
 *		       Version 2, June 1991
 *
 * Copyright (C) Linus Torvalds, 2005
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
 * These error routines were inspired / partially-coppied from the homonymous
 * usage.c and git-compat-util.h routines from the Git project[1], at commit
 * 88d915a634b44 ("A few fixes  before -rc2", 2021-11-04). The version
 * presented here was simplified as we don't need the more complex mechanics
 * and flexibility from the original ones.
 * [1]: https://github.com/git/git
 */

#ifndef _ERROR_H
#define _ERROR_H

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <stdnoreturn.h>

static void inline _error_common_errno(const char *prefix, const char *fmt,
			  va_list args, int errno_)
{
	fprintf(stderr, "%s: ", prefix);
	vfprintf(stderr, fmt, args);
	fprintf(stderr, ": %s\n", strerror(errno_));
}

static void inline _error_common(const char *prefix, const char *fmt,
			  va_list args)
{
	fprintf(stderr, "%s: ", prefix);
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
}

static int error_errno(char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	_error_common_errno("error", fmt, args, errno);
	va_end(args);
	return -1;
}

static int error(char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	_error_common("error", fmt, args);
	va_end(args);
	return -1;
}

static void warning(char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	_error_common("warning", fmt, args);
	va_end(args);
}

noreturn void die_errno(char *fmt, ...);
noreturn void die(char *fmt, ...);

noreturn void BUG_fl(const char *file, int line, const char *fmt, ...);
#define BUG(...) BUG_fl(__FILE__, __LINE__, __VA_ARGS__)

typedef void (*at_die_fn)(void);

void push_at_die(at_die_fn fn);
void pop_at_die(void);

#endif
