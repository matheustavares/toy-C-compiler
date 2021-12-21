#include <stdio.h>
#include <assert.h>
#include "parser.h"
#include "util.h"

struct x86_ctx {
	FILE *out;
};

#define emit(ctx, ...) \
	do { \
		if (fprintf((ctx)->out, __VA_ARGS__) < 0) \
			die_errno("fprintf error"); \
	} while (0)

static void generate_expression(struct ast_expression *exp, struct x86_ctx *ctx);

static char *label_or_skip_2nd_clause(void)
{
	static unsigned long counter = 0;
	return xmkstr("_or_skip_2nd_clause_%lu", counter++);
}

static void generate_logic_or(struct ast_expression *exp, struct x86_ctx *ctx)
{
	assert(exp->type == AST_EXP_BINARY_OP &&
	       exp->u.bin_op.type == EXP_OP_LOGIC_OR);

	char *label_skip_2nd_clause = label_or_skip_2nd_clause();

	generate_expression(exp->u.bin_op.lexp, ctx);
	emit(ctx, " cmp	$0, %%eax\n");
	emit(ctx, " jne	%s\n", label_skip_2nd_clause);
	generate_expression(exp->u.bin_op.rexp, ctx);
	emit(ctx, " cmp	$0, %%eax\n");
	emit(ctx, "%s:\n", label_skip_2nd_clause);
	emit(ctx, " mov	$0, %%eax\n");
	emit(ctx, " setne	%%al\n");
	free(label_skip_2nd_clause);
}

static char *label_and_skip_2nd_clause(void)
{
	static unsigned long counter = 0;
	return xmkstr("_and_skip_2nd_clause_%lu", counter++);
}

static void generate_logic_and(struct ast_expression *exp, struct x86_ctx *ctx)
{
	assert(exp->type == AST_EXP_BINARY_OP &&
	       exp->u.bin_op.type == EXP_OP_LOGIC_AND);

	char *label_skip_2nd_clause = label_and_skip_2nd_clause();

	generate_expression(exp->u.bin_op.lexp, ctx);
	emit(ctx, " cmp	$0, %%eax\n");
	emit(ctx, " je	%s\n", label_skip_2nd_clause);
	generate_expression(exp->u.bin_op.rexp, ctx);
	emit(ctx, " cmp	$0, %%eax\n");
	emit(ctx, "%s:\n", label_skip_2nd_clause);
	emit(ctx, " mov	$0, %%eax\n");
	emit(ctx, " setne	%%al\n");
	free(label_skip_2nd_clause);
}

/* Convention: generate_expression should put the result in eax. */
static void generate_expression(struct ast_expression *exp, struct x86_ctx *ctx)
{
	switch (exp->type) {
	case AST_EXP_BINARY_OP:
		enum bin_op_type bin_op_type = exp->u.bin_op.type;

		/*
		 * We check these first because they have their own semantics
		 * regarding the calculation (or not) of the right expresion.
		 */
		if (bin_op_type == EXP_OP_LOGIC_AND) {
			generate_logic_and(exp, ctx);
			return;
		} else if (bin_op_type == EXP_OP_LOGIC_OR) {
			generate_logic_or(exp, ctx);
			return;
		}

		/*
		 * "sub ecx eax" does "eax = eax - ecx". We calculate
		 * rexp first so that its value ends up in ecx and lexp
		 * ends up in eax, making them ready for the subtraction. We
		 * could, instead, manipulate the registers after the
		 * sub-expression is calculated, but it is easier to do it this
		 * way.
		 */
		generate_expression(exp->u.bin_op.rexp, ctx);
		/*
		 * Saving this in a register would be faster, but we don't know
		 * how many sub-expressions there is and register allocation is
		 * more complex than simplying pushing this value into the
		 * stack.
		 */
		emit(ctx, " push	%%rax\n");
		generate_expression(exp->u.bin_op.lexp, ctx);
		emit(ctx, " pop	%%rcx\n");

		switch (bin_op_type) {
		case EXP_OP_ADDITION:
			emit(ctx, " add	%%ecx, %%eax\n");
			break;
		case EXP_OP_SUBTRACTION:
			emit(ctx, " sub	%%ecx, %%eax\n");
			break;
		case EXP_OP_MULTIPLICATION:
			emit(ctx, " imul	%%ecx, %%eax\n");
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
			emit(ctx, " cdq\n");
			emit(ctx, " idiv	%%ecx\n");
			break;
		case EXP_OP_MODULO:
			emit(ctx, " cdq\n");
			emit(ctx, " idiv	%%ecx\n");
			/* idiv stores the remainder in edx. */
			emit(ctx, " mov	%%edx, %%eax\n");
			break;
		case EXP_OP_EQUAL:
			emit(ctx, " cmp	%%ecx, %%eax\n");
			emit(ctx, " mov	$0, %%eax\n");
			emit(ctx, " sete	%%al\n");
			break;
		case EXP_OP_NOT_EQUAL:
			emit(ctx, " cmp	%%ecx, %%eax\n");
			emit(ctx, " mov	$0, %%eax\n");
			emit(ctx, " setne	%%al\n");
			break;
		case EXP_OP_LT:
			emit(ctx, " cmp	%%ecx, %%eax\n");
			emit(ctx, " mov	$0, %%eax\n");
			emit(ctx, " setl	%%al\n");
			break;
		case EXP_OP_LE:
			emit(ctx, " cmp	%%ecx, %%eax\n");
			emit(ctx, " mov	$0, %%eax\n");
			emit(ctx, " setle	%%al\n");
			break;
		case EXP_OP_GT:
			emit(ctx, " cmp	%%ecx, %%eax\n");
			emit(ctx, " mov	$0, %%eax\n");
			emit(ctx, " setg	%%al\n");
			break;
		case EXP_OP_GE:
			emit(ctx, " cmp	%%ecx, %%eax\n");
			emit(ctx, " mov	$0, %%eax\n");
			emit(ctx, " setge	%%al\n");
			break;

		case EXP_OP_BITWISE_AND:
			emit(ctx, " and	%%ecx, %%eax\n");
			break;
		case EXP_OP_BITWISE_OR:
			emit(ctx, " or	%%ecx, %%eax\n");
			break;
		case EXP_OP_BITWISE_XOR:
			emit(ctx, " xor	%%ecx, %%eax\n");
			break;
		case EXP_OP_BITWISE_LEFT_SHIFT:
			emit(ctx, " shl	%%ecx, %%eax\n");
			break;
		case EXP_OP_BITWISE_RIGHT_SHIFT:
			emit(ctx, " shr	%%ecx, %%eax\n");
			break;

		default:
			die("generate x86: unknown binary op: %d", bin_op_type);
		}
		break;

	case AST_EXP_UNARY_OP:
		generate_expression(exp->u.un_op.exp, ctx);
		switch (exp->u.un_op.type) {
		case EXP_OP_NEGATION:
			emit(ctx, " neg	%%eax\n");
			break;
		case EXP_OP_BIT_COMPLEMENT:
			emit(ctx, " not	%%eax\n");
			break;
		case EXP_OP_LOGIC_NEGATION:
			emit(ctx, " cmp	$0, %%eax\n");
			emit(ctx, " mov	$0, %%eax\n");
			emit(ctx, " sete	%%al\n");
			break;
		default:
			die("generate x86: unknown unary op: %d", exp->u.un_op.type);
		}
		break;
	case AST_EXP_CONSTANT_INT:
		emit(ctx, " mov	$%d, %%eax\n", exp->u.ival);
		break;
	default:
		die("generate x86: unknown expression type %d", exp->type);
	}
}

static void generate_statement(struct ast_statement *st, struct x86_ctx *ctx)
{
	switch(st->type) {
	case AST_ST_RETURN:
		generate_expression(st->u.ret_exp, ctx);
		/* exp value is on eax, so just return it. */
		emit(ctx, " ret\n");
		break;
	default:
		die("gerate x86: unknown statement type %d", st->type);
	}
}

static void generate_func_decl(struct ast_func_decl *fun, struct x86_ctx *ctx)
{
	emit(ctx, " .globl %s\n", fun->name);
	emit(ctx, "%s:\n", fun->name);
	generate_statement(fun->body, ctx);
}

static void generate_prog(struct ast_program *prog, struct x86_ctx *ctx)
{
	generate_func_decl(prog->fun, ctx);
}

static const char *x86_out_filename;
static void x86_cleanup(void)
{
	if (x86_out_filename && unlink(x86_out_filename))
		error_errno("failed to remove temporary asm file '%s'",
			    x86_out_filename);
}

void generate_x86_asm(struct ast_program *prog, const char *out_filename)
{
	struct x86_ctx ctx;
	FILE *file = fopen(out_filename, "w");
	if (!file)
		die_errno("failed to open out file '%s'", out_filename);

	x86_out_filename = out_filename;
	push_at_die(x86_cleanup);

	ctx.out = file;

	generate_prog(prog, &ctx);

	pop_at_die();

	if (fclose(file))
		error_errno("failed to close '%s'", out_filename);
}
