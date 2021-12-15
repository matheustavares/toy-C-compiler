#include <stdio.h>
#include "parser.h"
#include "util.h"

#define xfprintf(file, ...) \
	do { \
		if (fprintf(file, __VA_ARGS__) < 0) \
			die_errno("fprintf error"); \
	} while (0)

/* Convention: generate_expression should put the result in eax. */
static void generate_expression(struct ast_expression *exp, FILE *file)
{
	switch (exp->type) {
	case AST_EXP_UNARY_OP:
		generate_expression(exp->u.un_op.exp, file);
		switch (exp->u.un_op.type) {
		case EXP_OP_NEGATION:
			xfprintf(file, " neg	%%eax\n");
			break;
		case EXP_OP_BIT_COMPLEMENT:
			xfprintf(file, " not	%%eax\n");
			break;
		case EXP_OP_LOGIC_NEGATION:
			xfprintf(file, " cmpl	$0, %%eax\n");
			xfprintf(file, " movl	$0, %%eax\n");
			xfprintf(file, " sete	%%al\n");
			break;
		default:
			die("generate x86: unknown unary op: %d", exp->u.un_op.type);
		}
		break;
	case AST_EXP_CONSTANT_INT:
		xfprintf(file, " movl	$%d, %%eax\n", exp->u.ival);
		break;
	default:
		die("generate x86: unknown expression type %d", exp->type);
	}
}

static void generate_statement(struct ast_statement *st, FILE *file)
{
	switch(st->type) {
	case AST_ST_RETURN:
		generate_expression(st->u.ret_exp, file);
		/* exp value is on eax, so just return it. */
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
