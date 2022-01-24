/*
 * 		    GNU GENERAL PUBLIC LICENSE
 *		       Version 2, June 1991
 *
 * Copyright (C) 2005-2022 Git Project
 * Copyright (C) 2022 Matheus Tavares
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
 * This file was copied from the Git project[1], at commit
 * 88d915a634b44 ("A few fixes  before -rc2", 2021-11-04), and modified 
 * (at 2021-01-23) to:
 * - Remove dependencies from other Git code.
 * - Simplify/remove functionalities that were not needed for this
 *   project.
 * - Adjust to the API of this project.
 * The signal handler code was partially copied from the sichain.[ch]
 * files from the Git project.
 * [1]: https://github.com/git/git
 */

/*
 * State diagram and cleanup
 * -------------------------
 *
 * If the program exits while a temporary file is active, we want to
 * make sure that we remove it. This is done by remembering the active
 * temporary files in a linked list, `tempfile_list`. An `atexit(3)`
 * handler and a signal handler are registered, to clean up any active
 * temporary files.
 *
 * Because the signal handler can run at any time, `tempfile_list` and
 * the `tempfile` objects that comprise it must be kept in
 * self-consistent states at all times.
 *
 * The possible states of a `tempfile` object are as follows:
 *
 * - Uninitialized. In this state the object's `on_list` field must be
 *   zero but the rest of its contents need not be initialized. As
 *   soon as the object is used in any way, it is irrevocably
 *   registered in `tempfile_list`, and `on_list` is set.
 *
 * - Active, file open (after `create_tempfile()` or
 *   `reopen_tempfile()`). In this state:
 *
 *   - the temporary file exists
 *   - `active` is set
 *   - `filename` holds the filename of the temporary file
 *   - `fd` holds a file descriptor open for writing to it
 *   - `fp` holds a pointer to an open `FILE` object if and only if
 *     `fdopen_tempfile()` has been called on the object
 *   - `owner` holds the PID of the process that created the file
 *
 * - Active, file closed (after `close_tempfile_gently()`). Same
 *   as the previous state, except that the temporary file is closed,
 *   `fd` is -1, and `fp` is `NULL`.
 *wait $! 2>/dev/null
 * - Inactive (after `delete_tempfile()`, `rename_tempfile()`, or a
 *   failed attempt to create a temporary file). In this state:
 *
 *   - `active` is unset
 *   - `filename` is empty (usually, though there are transitory
 *     states in which this condition doesn't hold). Client code should
 *     *not* rely on the filename being empty in this state.
 *   - `fd` is -1 and `fp` is `NULL`
 *   - the object is removed from `tempfile_list` (but could be used again)
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include "error.h"
#include "array.h"
#include "tempfile.h"

static ARRAY(struct tempfile *) tempfile_list;

static void unlink_or_warn(const char *path)
{
	if (unlink(path))
		error_errno("failed to unlink temporary file '%s'", path);
}

static void remove_tempfiles(int in_signal_handler)
{
	for (int i = 0; i < tempfile_list.nr; i++) {
		struct tempfile *p = tempfile_list.arr[i];

		if (!is_tempfile_active(p))
			continue;

		if (p->fd >= 0)
			close(p->fd);

		if (in_signal_handler)
			unlink(p->filename);
		else
			unlink_or_warn(p->filename);

		p->active = 0;
	}
	FREE_ARRAY(&tempfile_list);
}

static void remove_tempfiles_on_exit(void)
{
	remove_tempfiles(0);
}

typedef void (*sighandler_t)(int);
static void xsignal(int sig, sighandler_t f)
{
	if (signal(sig, f) == SIG_ERR)
		die_errno("failed to set signal handler");
}

static void signal_set_common(sighandler_t f)
{
	xsignal(SIGINT, f);
	xsignal(SIGHUP, f);
	xsignal(SIGTERM, f);
	xsignal(SIGQUIT, f);
	xsignal(SIGPIPE, f);
}

static void remove_tempfiles_on_signal(int signo)
{
	remove_tempfiles(1);
	signal_set_common(SIG_DFL);
	raise(signo);
}

static struct tempfile *new_tempfile(void)
{
	struct tempfile *tempfile = xmalloc(sizeof(*tempfile));
	tempfile->fd = -1;
	tempfile->fp = NULL;
	tempfile->active = 0;
	return tempfile;
}

static void activate_tempfile(struct tempfile *tempfile)
{
	static int initialized;

	if (is_tempfile_active(tempfile))
		BUG("activate_tempfile called for active object");

	if (!initialized) {
		ARRAY_INIT(&tempfile_list);
		signal_set_common(remove_tempfiles_on_signal);
		atexit(remove_tempfiles_on_exit);
		initialized = 1;
	}

	ARRAY_APPEND(&tempfile_list, tempfile);
	tempfile->active = 1;
}

static void deactivate_tempfile(struct tempfile *tempfile)
{
	tempfile->active = 0;
	free(tempfile->filename);
	ARRAY_REMOVE(&tempfile_list, tempfile);
	free(tempfile);
}

/* Make sure errno contains a meaningful value on error */
struct tempfile *create_tempfile_mode(const char *path, int mode)
{
	struct tempfile *tempfile = new_tempfile();

	tempfile->filename = xstrdup(path);
	tempfile->fd = open(tempfile->filename,
			    O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, mode);
	if (O_CLOEXEC && tempfile->fd < 0 && errno == EINVAL)
		/* Try again w/o O_CLOEXEC: the kernel might not support it */
		tempfile->fd = open(tempfile->filename,
				    O_RDWR | O_CREAT | O_EXCL, mode);
	if (tempfile->fd < 0) {
		deactivate_tempfile(tempfile);
		return NULL;
	}
	activate_tempfile(tempfile);

	return tempfile;
}

struct tempfile *mktempfile_s(const char *filename_template, size_t suffixlen)
{
	struct tempfile *tempfile = new_tempfile();
	tempfile->filename = xstrdup(filename_template);
	tempfile->fd = mkstemps(tempfile->filename, suffixlen);
	if (tempfile->fd < 0) {
		deactivate_tempfile(tempfile);
		return NULL;
	}
	activate_tempfile(tempfile);
	return tempfile;
}

FILE *fdopen_tempfile(struct tempfile *tempfile, const char *mode)
{
	if (!is_tempfile_active(tempfile))
		BUG("fdopen_tempfile() called for inactive object");
	if (tempfile->fp)
		BUG("fdopen_tempfile() called for open object");

	tempfile->fp = fdopen(tempfile->fd, mode);
	return tempfile->fp;
}

const char *get_tempfile_path(struct tempfile *tempfile)
{
	if (!is_tempfile_active(tempfile))
		BUG("get_tempfile_path() called for inactive object");
	return tempfile->filename;
}

int get_tempfile_fd(struct tempfile *tempfile)
{
	if (!is_tempfile_active(tempfile))
		BUG("get_tempfile_fd() called for inactive object");
	return tempfile->fd;
}

FILE *get_tempfile_fp(struct tempfile *tempfile)
{
	if (!is_tempfile_active(tempfile))
		BUG("get_tempfile_fp() called for inactive object");
	return tempfile->fp;
}

int close_tempfile_gently(struct tempfile *tempfile)
{
	int fd;
	FILE *fp;
	int err;

	if (!is_tempfile_active(tempfile) || tempfile->fd < 0)
		return 0;

	fd = tempfile->fd;
	fp = tempfile->fp;
	tempfile->fd = -1;
	if (fp) {
		tempfile->fp = NULL;
		if (ferror(fp)) {
			err = -1;
			if (!fclose(fp))
				errno = EIO;
		} else {
			err = fclose(fp);
		}
	} else {
		err = close(fd);
	}

	return err ? -1 : 0;
}

int reopen_tempfile(struct tempfile *tempfile)
{
	if (!is_tempfile_active(tempfile))
		BUG("reopen_tempfile called for an inactive object");
	if (0 <= tempfile->fd)
		BUG("reopen_tempfile called for an open object");
	tempfile->fd = open(tempfile->filename, O_WRONLY|O_TRUNC);
	return tempfile->fd;
}

int rename_tempfile(struct tempfile **tempfile_p, const char *path)
{
	struct tempfile *tempfile = *tempfile_p;

	if (!is_tempfile_active(tempfile))
		BUG("rename_tempfile called for inactive object");

	if (close_tempfile_gently(tempfile)) {
		delete_tempfile(tempfile_p);
		return -1;
	}

	if (rename(tempfile->filename, path)) {
		int save_errno = errno;
		delete_tempfile(tempfile_p);
		errno = save_errno;
		return -1;
	}

	deactivate_tempfile(tempfile);
	*tempfile_p = NULL;
	return 0;
}

void delete_tempfile(struct tempfile **tempfile_p)
{
	struct tempfile *tempfile = *tempfile_p;

	if (!is_tempfile_active(tempfile))
		return;

	close_tempfile_gently(tempfile);
	unlink_or_warn(tempfile->filename);
	deactivate_tempfile(tempfile);
	*tempfile_p = NULL;
}

int commit_tempfile(struct tempfile **tempfile_p)
{
	struct tempfile *tempfile = *tempfile_p;

	if (!is_tempfile_active(tempfile))
		return 0;

	if (close_tempfile_gently(tempfile)) {
		return error_errno("failed to close tempfile '%s'",
				   get_tempfile_path(tempfile));
	}
	deactivate_tempfile(tempfile);
	*tempfile_p = NULL;
	return 0;
}
