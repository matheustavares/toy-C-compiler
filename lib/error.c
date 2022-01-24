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
 * Some of these error routines were inspired / partially-coppied from the
 * homonymous usage.c and git-compat-util.h routines from the Git project[1],
 * at commit 88d915a634b44 ("A few fixes  before -rc2", 2021-11-04). The
 * version presented here was simplified as we don't need the more complex
 * mechanics and flexibility from the original ones.
 * [1]: https://github.com/git/git
 */

#include "error.h"

static at_die_fn at_die;

static int die_is_recursing(void)
{
	static int dying;
	return dying++;
}

noreturn void die_errno(char *fmt, ...)
{
	va_list args;
	if (die_is_recursing()) {
		fputs("fatal: recursion detected in die handler\n", stderr);
		exit(128);
	}
	va_start(args, fmt);
	_error_common_errno("error", fmt, args, errno);
	va_end(args);
	if (at_die)
		at_die();
	exit(128);
}

noreturn void die(char *fmt, ...)
{
	va_list args;
	if (die_is_recursing()) {
		fputs("fatal: recursion detected in die handler\n", stderr);
		exit(128);
	}
	va_start(args, fmt);
	_error_common("fatal", fmt, args);
	va_end(args);
	if (at_die)
		at_die();
	exit(128);
}

noreturn void BUG_fl(const char *file, int line, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	fprintf(stderr, "BUG: %s:%d: ", file, line);
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
	va_end(args);
	abort();
}

void push_at_die(at_die_fn fn)
{
	if (at_die)
		die("BUG: push_at_die currently can only hold one entry");
	at_die = fn;
}

void pop_at_die(void)
{
	if (!at_die)
		die("BUG: pop_at_die called with empty at_die stack");
	at_die = NULL;
}
