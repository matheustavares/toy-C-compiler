#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "util.h"
#include "lexer.h"

static void usage(const char *progname)
{
	die("usage: %s <c-file>", progname);
}

static char *read_file(const char *filename)
{
	char *buf;
	struct stat st;
	size_t to_read;
	int fd;

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		die_errno("failed to open '%s'", filename);

	if (fstat(fd, &st))
		die_errno("fstat failed");

	buf = xmalloc(st.st_size + 1);

	for (to_read = st.st_size; to_read; ) {
		ssize_t ret = read(fd, buf, to_read);
		if (ret < 0)
			die_errno("read error");
		else if (!ret)
			die("BUG: premature end of file");
		to_read -= ret;
	}

	buf[st.st_size] = '\0';
	close(fd);
	return buf;
}

int main(int argc, char **argv)
{
	char *file_buf;
	size_t nr_tokens;
	struct token *tokens;

	if (argc != 2)
		usage(*argv);

	file_buf = read_file(argv[1]);
	tokens = lex(file_buf, &nr_tokens);
	
	for (size_t i = 0; i < nr_tokens; i++) {
		print_token(&tokens[i]);
		free_token(&tokens[i]);
	}

	free(tokens);
	free(file_buf);

	return 0;
}
