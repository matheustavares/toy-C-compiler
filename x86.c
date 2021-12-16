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
	case AST_EXP_BINARY_OP:
		/*
		 * "sub ecx eax" does "eax = eax - ecx". We calculate
		 * rexp first so that its value ends up in ecx and lexp
		 * ends up in eax, making them ready for the subtraction. We
		 * could, instead, manipulate the registers after the
		 * sub-expression is calculated, but it is easier to do it this
		 * way.
		 */
		generate_expression(exp->u.bin_op.rexp, file);
		/*
		 * Saving this in a register would be faster, but we don't know
		 * how many sub-expressions there is and register allocation is
		 * more complex than simplying pushing this value into the
		 * stack.
		 */
		xfprintf(file, " push	%%rax\n");
		generate_expression(exp->u.bin_op.lexp, file);
		xfprintf(file, " pop	%%rcx\n");

		switch (exp->u.bin_op.type) {
		case EXP_OP_ADDITION:
			xfprintf(file, " add	%%ecx, %%eax\n");
			break;
		case EXP_OP_SUBTRACTION:
			xfprintf(file, " sub	%%ecx, %%eax\n");
			break;
		case EXP_OP_MULTIPLICATION:
			xfprintf(file, " imul	%%ecx, %%eax\n");
			break;
		case EXP_OP_DIVISION:
			/*
			 * "idiv %ecx" does "eax = edx:eax // ecx". But edx
			 * might already have some data, and we wouldn't want
			 * to use random bits in our division. At first, it
			 * seems that zeroing it would do the trick, but that
			 * would break negative division, since '0*64:eax'
			 * would represent a different number than 'eax' when
			 * eax is negative. So we use cdq, which does a sign
			 * extension of eax into edx:eax.
			 */
			xfprintf(file, " cdq\n");
			xfprintf(file, " idiv	%%ecx\n");
			break;
		default:
			die("generate x86: unknown binary op: %d", exp->u.bin_op.type);
		}
		break;

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
			xfprintf(file, " cmp	$0, %%eax\n");
			xfprintf(file, " mov	$0, %%eax\n");
			xfprintf(file, " sete	%%al\n");
			break;
		default:
			die("generate x86: unknown unary op: %d", exp->u.un_op.type);
		}
		break;
	case AST_EXP_CONSTANT_INT:
		xfprintf(file, " mov	$%d, %%eax\n", exp->u.ival);
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
