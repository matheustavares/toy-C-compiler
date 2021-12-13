#ifndef _UTIL_H
#define _UTIL_H

#include "lib/error.h"
#include "lib/wrappers.h"

static char *getline_dup(const char *str)
{
	const char *newline = strchr(str, '\n');
	if (!newline)
		return xstrdup(str);
	size_t size = newline - str;
	char *line = xmalloc(size + 1);
	return strncpy(line, str, size);
}

/*
 * If the string "str" begins with the string found in "prefix", return 1.
 * The "out" parameter is set to "str + strlen(prefix)" (i.e., to the point in
 * the string right after the prefix).
 *
 * Otherwise, return 0 and leave "out" untouched.
 *
 * Examples:
 *
 *   [extract branch name, fail if not a branch]
 *   if (!skip_prefix(ref, "refs/heads/", &branch)
 *	return -1;
 *
 *   [skip prefix if present, otherwise use whole string]
 *   skip_prefix(name, "refs/heads/", &name);
 *
 *   Copied from Git's (https://github.com/git/git) git-compat-util.h, at
 *   commit 88d915a634b44 ("A few fixes before -rc2", 2021-11-04). (GPL v2.0)
 */
static inline int skip_prefix(const char *str, const char *prefix,
			      const char **out)
{
	do {
		if (!*prefix) {
			*out = str;
			return 1;
		}
	} while (*str++ == *prefix++);
	return 0;
}

/*
 * If buf ends with suffix, return 1 and subtract the length of the suffix
 * from *len. Otherwise, return 0 and leave *len untouched.
 *
 * Copied from Git's (https://github.com/git/git) git-compat-util.h, at
 * commit 88d915a634b44 ("A few fixes before -rc2", 2021-11-04). (GPL v2.0)
 */
static inline int strip_suffix_mem(const char *buf, size_t *len,
				   const char *suffix)
{
	size_t suflen = strlen(suffix);
	if (*len < suflen || memcmp(buf + (*len - suflen), suffix, suflen))
		return 0;
	*len -= suflen;
	return 1;
}

/*
 * If str ends with suffix, return 1 and set *len to the size of the string
 * without the suffix. Otherwise, return 0 and set *len to the size of the
 * string.
 *
 * Note that we do _not_ NUL-terminate str to the new length.
 *
 * Copied from Git's (https://github.com/git/git) git-compat-util.h, at
 * commit 88d915a634b44 ("A few fixes before -rc2", 2021-11-04). (GPL v2.0)
 */
static inline int strip_suffix(const char *str, const char *suffix, size_t *len)
{
	*len = strlen(str);
	return strip_suffix_mem(str, len, suffix);
}

/*
 * Copied from Git's (https://github.com/git/git) strbuf.c, where it was
 * originally named strbuf_vaddf(). At commit 88d915a634b44 ("A few fixes
 * before -rc2", 2021-11-04). (GPL v2.0) It was modified to allocate and
 * return a "char *" buffer, instead of work on a struct strbuf.
 */
#define MKSTR_INITIAL_SIZE 512
static char *xmkstr(const char *fmt, ...)
{
	size_t size = MKSTR_INITIAL_SIZE;
	char *buf = xmalloc(size);
	va_list args;

	va_start(args, fmt);

	int ret = vsnprintf(buf, size, fmt, args);
	if (ret < 0) {
		die_errno("vsnprintf error");
	} else if (ret >= size) {
		size = ret + 1;
		buf = xrealloc(buf, size);
		ret = vsnprintf(buf, size, fmt, args);
		if (ret < 0)
			die_errno("vsnprintf error");
		else if (ret >= size)
			die("BUG: vsnprintf didn't honor its previous size request");
	}

	va_end(args);
	return buf;
}

#endif
