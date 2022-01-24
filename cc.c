#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "util.h"
#include "lexer.h"
#include "parser.h"
#include "dot-printer.h"
#include "x86.h"
#include "lib/tempfile.h"

static void usage(const char *progname, int err)
{
	fprintf(stderr, "usage: %s [options] <c-file>\n", progname);
	fprintf(stderr, "       -h|--help: this message\n");
	fprintf(stderr, "       -l|--lex:  print the lex'ed tokens\n");
	fprintf(stderr, "       -t|--tree: print the parsed tree in dot format\n");
	fprintf(stderr, "       -c:        do not link, only produce an object file\n");
	fprintf(stderr, "       -S:        leave the asm file and don't generate the binary\n");
	fprintf(stderr, "       -o <file>: the pathname for the output file\n");

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

static char *asm_filename_from_source(const char *source_filename)
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

static char *bin_filename_from_source(const char *source_filename, int link)
{
	size_t base_len;
	if (!strip_suffix(source_filename, ".c", &base_len))
		die("expected input file with .c suffix");
	char *out = xmkstr("%.*s%s", base_len, source_filename, link ? "" : ".o");
	return out;
}

static void assemble(const char *asm_filename, char *out_filename, int link)
{
	int sys_ret;
	char *assembler_cmd;

	/* TODO: avoid calling system() with string derivated from user input? */
	assembler_cmd = xmkstr("gcc %s %s -o %s", link ? "" : "-c",
			       asm_filename, out_filename);
	sys_ret = system(assembler_cmd);

	if (sys_ret == -1)
		die_errno("system() failed");
	else if (sys_ret)
		die("failed to call gcc to assemble the binary");
}

static int has_suffix(const char *filename, const char *expected_suffix)
{
	size_t len;
	return strip_suffix(filename, expected_suffix, &len);
}

int main(int argc, char **argv)
{
	char *source_buf;
	struct token *tokens;
	struct ast_program *prog;
	char **arg_cursor, *out_filename = NULL;
	struct tempfile *asm_file;
	const char *value;
	int print_lex = 0,
	    print_tree = 0,
	    stop_at_assembly = 0,
	    link = 1;

	for (arg_cursor = argv + 1; *arg_cursor; arg_cursor++) {
		if (*arg_cursor[0] != '-') {
			break; /* not an option */
		} else if (!strcmp(*arg_cursor, "-h") || !strcmp(*arg_cursor, "--help")) {
			usage(*argv, 0);
		} else if (!strcmp(*arg_cursor, "-l") || !strcmp(*arg_cursor, "--lex")) {
			print_lex = 1;
		} else if (!strcmp(*arg_cursor, "-t") || !strcmp(*arg_cursor, "--tree")) {
			print_tree = 1;
		} else if (!strcmp(*arg_cursor, "-c")) {
			link = 0;
		} else if (skip_prefix(*arg_cursor, "-o", &value)) {
			if (!*value) {
				arg_cursor++;
				value = *arg_cursor;
			}
			if (!value || value[0] == '-')
				die("-o requires a value");
			out_filename = xstrdup(value);
		} else if (!strcmp(*arg_cursor, "-S")) {
			stop_at_assembly = 1;
		} else {
			die("unknown option '%s'", *arg_cursor);
		}
	}

	if (!*arg_cursor || *(arg_cursor + 1)) {
		error("expecting one positional argument: the C filename");
		usage(*argv, 1);
	}
	char *source = *arg_cursor;

	if (print_tree && print_lex)
		die("--lex and --tree are incompatible");
	if ((stop_at_assembly || !link) && (print_tree || print_lex))
		die("-S and -c are incompatible with --lex and --tree");

	if (!has_suffix(source, ".c"))
		die("input file must have .c suffix");

	/********************* LEXER and PARSER *********************/

	source_buf = read_file(source);
	tokens = lex(source_buf);

	if (print_lex) {
		print_tokens(tokens);
		goto lex_out;
	}

	prog = parse_program(tokens);

	if (print_tree) {
		print_ast_in_dot(prog);
		goto ast_out;
	}

	/************************ ASSEMBLY **************************/

	if (stop_at_assembly) {
		char *asm_filename = out_filename ? xstrdup(out_filename) :
				     asm_filename_from_source(source);
		asm_file = create_tempfile(asm_filename, 1);
		free(asm_filename);
	} else {
		asm_file = mktempfile_s(".tmp-asm-XXXXXX.s", 2);
	}

	if (!fdopen_tempfile(asm_file, "w"))
		die_errno("fdopen error on '%s'", get_tempfile_path(asm_file));

	generate_x86_asm(prog, get_tempfile_fp(asm_file));

	if (close_tempfile_gently(asm_file))
		error_errno("failed to close '%s'", get_tempfile_path(asm_file));

	if (stop_at_assembly) {
		if (commit_tempfile(&asm_file))
			die("failed to close assembly file");
		goto asm_out;
	}

	/************************* BINARY ***************************/

	char *bin_filename = out_filename ? xstrdup(out_filename) :
		       bin_filename_from_source(source, link);
	assemble(get_tempfile_path(asm_file), bin_filename, link);
	free(bin_filename);

asm_out:
ast_out:
	free_ast(prog);
lex_out:
	free_tokens(tokens);
	free(source_buf);

	return 0;
}
