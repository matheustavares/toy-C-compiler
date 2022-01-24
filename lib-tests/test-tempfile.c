#include <stdio.h>
#include <stdlib.h>
#include "../util.h"
#include "../lib/tempfile.h"

int main(int argc, char **argv)
{
	const char *val;
	struct tempfile *t = NULL, *t2 = NULL;

	for (argv++; *argv; argv++) {
		if (!strcmp(*argv, "-h") || !strcmp(*argv, "--help")) {
			printf("Options:\n");
			printf("    create-template=<str>\n");
			printf("    create-template-s=<str>,<int>\n");
			printf("    create-path=<str>\n");
			printf("    exit\n");
			printf("    signal\n");
			printf("    remove\n");
			printf("    rename=<str>\n");
			printf("    commit\n");
			printf("    switch-pointer\n");
			printf("    write-fd=<str>\n");
			printf("    write-fp=<str>\n");
			return 0;
		} else if (skip_prefix(*argv, "create-template=", &val)) {
			printf("create-template '%s'\n", val);
			t = mktempfile(val);
			if (!t)
				die("failed to create tempfile");
		} else if (skip_prefix(*argv, "create-template-s=", &val)) {
			const char *comma;
			for (comma = val; *comma && *comma != ','; comma++)
				;
			if (!*comma || !*(++comma))
				die("unknown option '%s'", *argv);
		
			const char *str = xstrndup(val, comma - val - 1);
			int suffixlen = atoi(comma);

			printf("create-template-s '%s',%d\n", str, suffixlen);
			t = mktempfile_s(str, suffixlen);
			if (!t)
				die_errno("failed to create tempfile");
			free((char *)str);
		} else if (skip_prefix(*argv, "create-path=", &val)) {
			printf("create-path '%s'\n", val);
			t = create_tempfile(val, 0);
			if (!t)
				die("failed to create tempfile");
		} else if (!strcmp(*argv, "exit")) {
			printf("exit\n");
			return 0;
		} else if (!strcmp(*argv, "signal")) {
			printf("signal\n");
			fflush(stdout);
			raise(SIGINT);
			return 0;
		} else if (!strcmp(*argv, "remove")) {
			printf("remove\n");
			delete_tempfile(&t);
		} else if (skip_prefix(*argv, "rename=", &val)) {
			printf("rename '%s'\n", val);
			if (rename_tempfile(&t, val))
				die_errno("failed to rename tempfile");
		} else if (!strcmp(*argv, "commit")) {
			printf("commit\n");
			if (commit_tempfile(&t))
				die_errno("failed to commit tempfile");
		} else if (!strcmp(*argv, "switch-pointer")) {
			printf("switch-pointer\n");
			struct tempfile *aux = t;
			t = t2;
			t2 = aux;
		} else if (skip_prefix(*argv, "write-fd=", &val)) {
			int fd = get_tempfile_fd(t);
			printf("write-fd '%s'\n", val);
			write(fd, val, strlen(val));
			write(fd, "\n", 1);
		} else if (skip_prefix(*argv, "write-fp=", &val)) {
			FILE *fp = get_tempfile_fp(t);
			if (!fp && !(fp = fdopen_tempfile(t, "w")))
				die_errno("fdopen failed");
			printf("write-fp '%s'\n", val);
			fprintf(fp, "%s\n", val);
		} else {
			die("unknown option '%s'", *argv);
		}
	}

	return 0;
}
