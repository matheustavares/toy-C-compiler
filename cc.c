#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "util.h"
#include "lexer.h"
#include "parser.h"
#include "x86.h"

static void usage(const char *progname, int err)
{
	fprintf(stderr, "usage: %s [options] <c-file>\n", progname);
	fprintf(stderr, "       -h|--help: this message\n");
	fprintf(stderr, "       -l|--lex:  print the lex'ed tokens\n");
	fprintf(stderr, "       -t|--tree: print the parsed tree in dot format\n");

	exit(err ? 129 : 0);
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

static void print_tokens(struct token *tokens)
{
	for (struct token *tok = tokens; !end_token(tok); tok++)
		print_token(tok);
}

static char *mk_out_filename(const char *source_filename)
{
	size_t len;
	strip_suffix(source_filename, ".c", &len);
	char *out = xmalloc(len + 3);
	memcpy(out, source_filename, len);
	memcpy(out + len, ".s", 2);
	out[len + 2] = '\0';
	return out;
}

int main(int argc, char **argv)
{
	char *file_buf;
	struct token *tokens;
	struct ast_program *prog;
	char **arg_cursor, *out_filename;
	int print_lex = 0, print_tree = 0;

	for (arg_cursor = argv + 1; *arg_cursor; arg_cursor++) {
		if (*arg_cursor[0] != '-')
			break; /* not an option */
		else if (!strcmp(*arg_cursor, "-h") || !strcmp(*arg_cursor, "--help"))
			usage(*argv, 0);
		else if (!strcmp(*arg_cursor, "-l") || !strcmp(*arg_cursor, "--lex"))
			print_lex = 1;
		else if (!strcmp(*arg_cursor, "-t") || !strcmp(*arg_cursor, "--tree"))
			print_tree = 1;
		else
			die("unknown option '%s'", *arg_cursor);
	}

	if (!*arg_cursor || *(arg_cursor + 1)) {
		error("expecting one positional argument: the C filename");
		usage(*argv, 1);
	}

	if (print_tree && print_lex)
		die("--lex and --tree are incompatible");

	file_buf = read_file(*arg_cursor);
	tokens = lex(file_buf);

	if (print_lex) {
		print_tokens(tokens);
		goto lex_out;
	}

	prog = parse_program(tokens);

	if (print_tree) {
		print_ast_in_dot(prog);
		goto ast_out;
	}
	
	out_filename = mk_out_filename(*arg_cursor);
	generate_x86_asm(prog, out_filename);
	fprintf(stderr, "Assembly written to '%s'\n", out_filename);

	free(out_filename);
ast_out:
	free_ast(prog);
lex_out:
	free_tokens(tokens);
	free(file_buf);

	return 0;
}
