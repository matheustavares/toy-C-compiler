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
 * [1]: https://github.com/git/git
 */

#ifndef TEMPFILE_H
#define TEMPFILE_H

#include <signal.h>

/*
 * Handle temporary files.
 *
 * The tempfile API allows temporary files to be created, deleted, and
 * atomically renamed. Temporary files that are still active when the
 * program ends are cleaned up automatically.
 *
 * Calling sequence
 * ----------------
 *
 * The caller:
 *
 * * Attempts to create a temporary file by calling
 *   `create_tempfile()`. The resources used for the temporary file are
 *   managed by the tempfile API.
 *
 * * Writes new content to the file by either:
 *
 *   * writing to the `tempfile->fd` file descriptor
 *
 *   * calling `fdopen_tempfile()` to get a `FILE` pointer for the
 *     open file and writing to the file using stdio.
 *
 *   Note that the file descriptor created by create_tempfile()
 *   is marked O_CLOEXEC, so the new contents must be written by
 *   the current process, not any spawned one.
 *
 * When finished writing, the caller can:
 *
 * * Close the file descriptor and remove the temporary file by
 *   calling `delete_tempfile()`.
 *
 * * Close the temporary file and rename it atomically to a specified
 *   filename by calling `rename_tempfile()`. This relinquishes
 *   control of the file.
 *
 * * Close the file descriptor without removing or renaming the
 *   temporary file by calling `close_tempfile_gently()`, and later call
 *   `delete_tempfile()` or `rename_tempfile()`.
 *
 * After the temporary file is renamed or deleted, the `tempfile`
 * object is no longer valid and should not be reused.
 *
 * If the program exits before `rename_tempfile()` or
 * `delete_tempfile()` is called, an `atexit(3)` handler will close
 * and remove the temporary file.
 *
 * If you need to close the file descriptor yourself, do so by calling
 * `close_tempfile_gently()`. You should never call `close(2)` or `fclose(3)`
 * yourself, otherwise the `struct tempfile` structure would still
 * think that the file descriptor needs to be closed, and a later
 * cleanup would result in duplicate calls to `close(2)`. Worse yet,
 * if you close and then later open another file descriptor for a
 * completely different purpose, then the unrelated file descriptor
 * might get closed.
 *
 *
 * Error handling
 * --------------
 *
 * `create_tempfile()` returns an allocated tempfile on success or NULL
 * on failure. On errors, `errno` describes the reason for failure.
 *
 * `rename_tempfile()` and `close_tempfile_gently()` return 0 on success.
 * On failure they set `errno` appropriately and return -1.
 * `delete_tempfile()` and `rename` (but not `close`) do their best to
 * delete the temporary file before returning.
 */

struct tempfile {
	sig_atomic_t active;
	char *filename;
	int fd;
	FILE *fp;
};

/*
 * Attempt to create a temporary file at the specified `path`. Return
 * a tempfile (whose "fd" member can be used for writing to it), or
 * NULL on error. It is an error if a file already exists at that path.
 * Note that `mode` will be further modified by the umask, and possibly
 * `core.sharedRepository`, so it is not guaranteed to have the given
 * mode.
 */
struct tempfile *create_tempfile_mode(const char *path, int mode);

static inline struct tempfile *create_tempfile(const char *path)
{
	return create_tempfile_mode(path, 0666);
}

/*
 * Attempt to create and open a temporary file with names derived automatically
 * from a template, in the manner of mkstemps(), and arrange for them to be
 * deleted if the program ends before they are deleted explicitly.
 *
 * mktempfile() do not modify template. If the caller wants to
 * know the (relative) path of the file that was created, it can be
 * read from tempfile->filename.
 *
 * On success, the function return a tempfile whose "fd" member is open
 * for writing the temporary file. On errors, they return NULL and set
 * errno appropriately.
 */
struct tempfile *mktempfile_s(const char *filename_template, size_t suffixlen);
#define mktempfile(filename_template) mktempfile_s(filename_template, 0)

/*
 * Associate a stdio stream with the temporary file (which must still
 * be open). Return `NULL` (*without* deleting the file) on error. The
 * stream is closed automatically when `close_tempfile_gently()` is called or
 * when the file is deleted or renamed.
 */
FILE *fdopen_tempfile(struct tempfile *tempfile, const char *mode);

static inline int is_tempfile_active(struct tempfile *tempfile)
{
	return tempfile && tempfile->active;
}

/*
 * Return the path of the tempfile. The return value is a pointer to a
 * field within the tempfile object and should not be freed.
 */
const char *get_tempfile_path(struct tempfile *tempfile);

int get_tempfile_fd(struct tempfile *tempfile);
FILE *get_tempfile_fp(struct tempfile *tempfile);

/*
 * If the temporary file is still open, close it (and the file pointer
 * too, if it has been opened using `fdopen_tempfile()`) without
 * deleting the file. Return 0 upon success. On failure to `close(2)`,
 * return a negative value. Usually `delete_tempfile()` or `rename_tempfile()`
 * should eventually be called regardless of whether `close_tempfile_gently()`
 * succeeds.
 */
int close_tempfile_gently(struct tempfile *tempfile);

/*
 * Re-open a temporary file that has been closed using
 * `close_tempfile_gently()` but not yet deleted or renamed. This can be used
 * to implement a sequence of operations like the following:
 *
 * * Create temporary file.
 *
 * * Write new contents to file, then `close_tempfile_gently()` to cause the
 *   contents to be written to disk.
 *
 * * Pass the name of the temporary file to another program to allow
 *   it (and nobody else) to inspect or even modify the file's
 *   contents.
 *
 * * `reopen_tempfile()` to reopen the temporary file, truncating the existing
 *   contents. Write out the new contents.
 *
 * * `rename_tempfile()` to move the file to its permanent location.
 */
int reopen_tempfile(struct tempfile *tempfile);

/*
 * Close the file descriptor and/or file pointer and remove the
 * temporary file associated with `tempfile`. It is a NOOP to call
 * `delete_tempfile()` for a `tempfile` object that has already been
 * deleted or renamed.
 */
void delete_tempfile(struct tempfile **tempfile_p);

/*
 * Close the file descriptor and/or file pointer if they are still
 * open, and atomically rename the temporary file to `path`. `path`
 * must be on the same filesystem as the tempfile. Return 0 on
 * success. On failure, delete the temporary file and return -1, with
 * `errno` set to the value from the failing call to `close(2)` or
 * `rename(2)`. It is a bug to call `rename_tempfile()` for a
 * `tempfile` object that is not currently active.
 */
int rename_tempfile(struct tempfile **tempfile_p, const char *path);

/*
 * Close the file descriptor and/or file pointer if they are still
 * open, without removing nor renaming the file. The tempfile struct
 * is free'd.
 */
int commit_tempfile(struct tempfile **tempfile_p);

#endif /* TEMPFILE_H */
