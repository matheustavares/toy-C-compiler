#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "util.h"
#include "lexer.h"
#include "parser.h"
#include "dot-printer.h"
#include "x86.h"

static void usage(const char *progname, int err)
{
	fprintf(stderr, "usage: %s [options] <c-file>\n", progname);
	fprintf(stderr, "       -h|--help: this message\n");
	fprintf(stderr, "       -l|--lex:  print the lex'ed tokens\n");
	fprintf(stderr, "       -t|--tree: print the parsed tree in dot format\n");
	fprintf(stderr, "       -S:        leave the asm file and don't generate the binary\n");

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

static char *mk_asm_filename(const char *source_filename)
{
	size_t len;
	char *out;
	if (!strip_suffix(source_filename, ".c", &len))
		die("expected input file with .c suffix");
	out = xmalloc(len + 3);
	memcpy(out, source_filename, len);
	memcpy(out + len, ".s", 2);
	out[len + 2] = '\0';
	return out;
}

static void assemble_and_link(const char *asm_filename)
{
	size_t base_len;
	int sys_ret;
	char *bin_filename, *assembler_cmd;

	if (!strip_suffix(asm_filename, ".s", &base_len))
		die("BUG: generated asm file without .s extension: '%s'",
		    asm_filename);
	bin_filename = xstrndup(asm_filename, base_len);

	/* TODO: avoid calling system() with string derivated from user input? */
	assembler_cmd = xmkstr("gcc %s -o %s", asm_filename, bin_filename);
	sys_ret = system(assembler_cmd);

	if (sys_ret == -1)
		die_errno("system() failed");
	else if (sys_ret)
		die("failed to call gcc to assemble and link");

	free(bin_filename);
}

static int has_suffix(const char *filename, const char *expected_suffix)
{
	size_t len;
	return strip_suffix(filename, expected_suffix, &len);
}

int main(int argc, char **argv)
{
	char *file_buf;
	struct token *tokens;
	struct ast_program *prog;
	char **arg_cursor, *asm_filename;
	int print_lex = 0, print_tree = 0, stop_at_assembly = 0;

	for (arg_cursor = argv + 1; *arg_cursor; arg_cursor++) {
		if (*arg_cursor[0] != '-')
			break; /* not an option */
		else if (!strcmp(*arg_cursor, "-h") || !strcmp(*arg_cursor, "--help"))
			usage(*argv, 0);
		else if (!strcmp(*arg_cursor, "-l") || !strcmp(*arg_cursor, "--lex"))
			print_lex = 1;
		else if (!strcmp(*arg_cursor, "-t") || !strcmp(*arg_cursor, "--tree"))
			print_tree = 1;
		else if (!strcmp(*arg_cursor, "-S"))
			stop_at_assembly = 1;
		else
			die("unknown option '%s'", *arg_cursor);
	}

	if (!*arg_cursor || *(arg_cursor + 1)) {
		error("expecting one positional argument: the C filename");
		usage(*argv, 1);
	}

	if (print_tree && print_lex)
		die("--lex and --tree are incompatible");
	if (stop_at_assembly && (print_tree || print_lex))
		die("-S is incompatible with --lex and --tree");

	if (!has_suffix(*arg_cursor, ".c"))
		die("input file must have .c suffix");

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
	
	asm_filename = mk_asm_filename(*arg_cursor);
	generate_x86_asm(prog, asm_filename);
	if (stop_at_assembly)
		goto asm_out;

	assemble_and_link(asm_filename);
	if (unlink(asm_filename))
		error_errno("failed to remove temporary asm file '%s'", asm_filename);

asm_out:
	free(asm_filename);
ast_out:
	free_ast(prog);
lex_out:
	free_tokens(tokens);
	free(file_buf);

	return 0;
}
