#include <stdio.h>
#include "parser.h"
#include "util.h"

#define xfprintf(file, ...) \
	do { \
		if (fprintf(file, __VA_ARGS__) < 0) \
			die_errno("fprintf error"); \
	} while (0)

static void generate_statement(struct ast_statement *st, FILE *file)
{
	switch(st->type) {
	case AST_ST_RETURN:
		struct ast_expression *exp = st->u.ret_exp;
		if (exp->type != AST_EXP_CONSTANT_INT)
			die("generate x86: I don't know how to return non-constant expressions");
		xfprintf(file, " movl	$%d, %%eax\n", exp->u.ival);
		xfprintf(file, " ret\n");
		break;
	default:
		die("gerate x86: unknown statement type %d", st->type);
	}
}

static void generate_func_decl(struct ast_func_decl *fun, FILE *file)
{
	xfprintf(file, " .globl %s\n", fun->name);
	xfprintf(file, "%s:\n", fun->name);
	generate_statement(fun->body, file);
}

static void generate_prog(struct ast_program *prog, FILE *file)
{
	generate_func_decl(prog->fun, file);
}

void generate_x86_asm(struct ast_program *prog, const char *out_filename)
{
	FILE *file = fopen(out_filename, "w");
	if (!file)
		die_errno("failed to open out file '%s'", out_filename);

	generate_prog(prog, file);

	if (fclose(file))
		error_errno("failed to close '%s'", out_filename);
}
