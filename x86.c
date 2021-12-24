#include <stdio.h>
#include <assert.h>
#include "parser.h"
#include "util.h"
#include "symtable.h"

struct x86_ctx {
	FILE *out;
	struct symtable *symtable;
	/*
	 * Local variables are stored in the stack with a position relative to
	 * %rpb. The first available "spot" for a local variable in a function
	 * is "%rpb - 8" because the stack frame is initialized with the
	 * previous %rpb value (which occupies the first 8 bytes).
	 *
	 * The stack_index stores what is the next offset (relative to %rpb) to
	 * store a local variable. Remember that the stack grows "down", so
	 * even though we store unsigned size_t, the address should be
	 * interpreted as "%rpb - stack_index";
	 *
	 * This is valid *only* valid inside function generation, and should be
	 * 0 otherwise.
	 */
	size_t stack_index;
	/* unsigned long scope; */
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
		 * regarding the calculation (or not) of the expresions.
		 */
		if (bin_op_type == EXP_OP_LOGIC_AND) {
			generate_logic_and(exp, ctx);
			return;
		} else if (bin_op_type == EXP_OP_LOGIC_OR) {
			generate_logic_or(exp, ctx);
			return;
		} else if (bin_op_type == EXP_OP_ASSIGNMENT) {
			struct ast_expression *lexp = exp->u.bin_op.lexp;
			assert(lexp->type == AST_EXP_VAR);
			size_t stack_index = symtable_var_ref(ctx->symtable, &lexp->u.var);
			generate_expression(exp->u.bin_op.rexp, ctx);
			emit(ctx, " mov	%%rax, -%zu(%%rbp)\n", stack_index);
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
		size_t stack_index;
		struct ast_expression *un_op_val = exp->u.un_op.exp;
		generate_expression(un_op_val, ctx);
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
		case EXP_OP_PREFIX_INC:
			assert(un_op_val->type == AST_EXP_VAR);
			stack_index = symtable_var_ref(ctx->symtable, &un_op_val->u.var);
			emit(ctx, " add	$1, %%eax\n");
			emit(ctx, " mov	%%rax, -%zu(%%rbp)\n", stack_index);
			break;
		case EXP_OP_PREFIX_DEC:
			assert(un_op_val->type == AST_EXP_VAR);
			stack_index = symtable_var_ref(ctx->symtable, &un_op_val->u.var);
			emit(ctx, " sub	$1, %%eax\n");
			emit(ctx, " mov	%%rax, -%zu(%%rbp)\n", stack_index);
			break;
		case EXP_OP_SUFFIX_INC:
			assert(un_op_val->type == AST_EXP_VAR);
			stack_index = symtable_var_ref(ctx->symtable, &un_op_val->u.var);
			emit(ctx, " add	$1, -%zu(%%rbp)\n", stack_index);
			break;
		case EXP_OP_SUFFIX_DEC:
			assert(un_op_val->type == AST_EXP_VAR);
			stack_index = symtable_var_ref(ctx->symtable, &un_op_val->u.var);
			emit(ctx, " sub	$1, -%zu(%%rbp)\n", stack_index);
			break;
		default:
			die("generate x86: unknown unary op: %d", exp->u.un_op.type);
		}
		break;
	case AST_EXP_CONSTANT_INT:
		emit(ctx, " mov	$%d, %%eax\n", exp->u.ival);
		break;
	case AST_EXP_VAR:
		stack_index = symtable_var_ref(ctx->symtable, &exp->u.var);
		emit(ctx, " mov	-%zu(%%rbp), %%rax\n", stack_index);
		break;
	default:
		die("generate x86: unknown expression type %d", exp->type);
	}
}

static void generate_func_epilogue_and_ret(struct x86_ctx *ctx)
{
	/* epilogue: restore previous stack frame. */
	emit(ctx, " mov	%%rbp, %%rsp\n");
	emit(ctx, " pop	%%rbp\n");
	emit(ctx, " ret\n");
}

static void generate_statement(struct ast_statement *st, struct x86_ctx *ctx)
{
	switch(st->type) {
	case AST_ST_RETURN:
		generate_expression(st->u.ret_exp, ctx);
		/* exp value is on eax, so just return it. */
		generate_func_epilogue_and_ret(ctx);
		break;
	case AST_ST_VAR_DECL:
		struct ast_var_decl *decl = st->u.decl;
		/*
		 * Put the varname on the symbol table before generating the
		 * code for rexp due to the weird case of declaration with
		 * assignment to itself:
		 *		int v = v = 2;
		 */
		symtable_put_lvar(ctx->symtable, decl, ctx->stack_index);
		if (decl->value) {
			generate_expression(decl->value, ctx);
		} else {
			/* We don't really need to initialize it, but... */
			emit(ctx, " mov	$0, %%eax\n");
		}
		/*
		 * TODO: investigate if should push rax (and increment
		 * stack_index by 8) or eax (and increment by 4).
		 */
		emit(ctx, " push	%%rax\n");
		ctx->stack_index += 8;
		break;
	case AST_ST_EXPRESSION:
		generate_expression(st->u.exp, ctx);
		break;
	default:
		die("generate x86: unknown statement type %d", st->type);
	}
}

static void generate_func_decl(struct ast_func_decl *fun, struct x86_ctx *ctx)
{
	emit(ctx, " .globl %s\n", fun->name);
	emit(ctx, "%s:\n", fun->name);

	/* prologue: save previous rbp and create an empty stack frame. */
	emit(ctx, " push	%%rbp\n");
	emit(ctx, " mov	%%rsp, %%rbp\n");
	ctx->stack_index = 8; /* sizeof(%rbp) */

	struct ast_statement *st, *last_st = NULL;;
	for (st = fun->body; st; last_st = st, st = st->next)
		generate_statement(st, ctx);

	if (!last_st || last_st->type != AST_ST_RETURN) {
		/*
		 * If the function is missing a return statement, and it is
		 * the main() function, it should return 0. If it is not
		 * main(), the behavior is undefined. To keep uniformity, we
		 * will return 0 in both cases. But let's warn the user.
		 */
		warning("function '%s' is missing a return statement. Will return 0.",
			fun->name);
		emit(ctx, " mov	$0, %%eax\n");
		generate_func_epilogue_and_ret(ctx);
	}
	ctx->stack_index = 0;
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
	struct symtable symtable;
	FILE *file = fopen(out_filename, "w");
	if (!file)
		die_errno("failed to open out file '%s'", out_filename);

	x86_out_filename = out_filename;
	push_at_die(x86_cleanup);

	symtable_init(&symtable);
	ctx.symtable = &symtable;
	ctx.out = file;
	ctx.stack_index = 0;

	generate_prog(prog, &ctx);

	pop_at_die();

	if (fclose(file))
		error_errno("failed to close '%s'", out_filename);

	symtable_destroy(&symtable);
}
