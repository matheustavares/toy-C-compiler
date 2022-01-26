#include <stdio.h>
#include <assert.h>
#include "parser.h"
#include "util.h"
#include "symtable.h"
#include "lexer.h"
#include "lib/stack.h"
#include "lib/strmap.h"
#include "labelset.h"

/* 
 * In argument order. That is, first arg goes on rdi, second on rsi, and so on.
 * Sixth arg and beyond goes on stack.
 */
const char *func_call_regs[] = { "rdi", "rsi", "rdx", "rcx", "r8", "r9" };
#define NR_CALL_REGS (sizeof(func_call_regs) / sizeof(*func_call_regs))

struct x86_ctx {
	FILE *out;
	struct symtable *symtable;
	/*
	 * Local variables are stored in the stack with a position relative to
	 * %rbp. The stack_index helps to keep track of local variables
	 * allocations (for this reason, it is *only* valid inside function
	 * generation). This variable stores the offset of %rsp relative to
	 * %rsp. So, at any given moment, "-{stack_index}(%rbp)" will be the
	 * same as "(%rsp)" (if I don't forget to update the variable when
	 * altering the stack). To allocate a new local variable, the code
	 * must increment stack_index with the variable's size and them push
	 * the variable onto the stack (the order is not really important).
	 */
	size_t stack_index;
	unsigned long scope;
	struct stack continue_labels,
		     break_labels;

	struct ast_func_decl *cur_func;

	struct labelset user_labels;
};

#define emit(ctx, ...) \
	do { \
		if (fprintf((ctx)->out, __VA_ARGS__) < 0) \
			die_errno("fprintf error"); \
	} while (0)

static void generate_expression(struct ast_expression *exp, struct x86_ctx *ctx,
				int require_value);
static void generate_statement(struct ast_statement *st, struct x86_ctx *ctx);

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

	generate_expression(exp->u.bin_op.lexp, ctx, 1);
	emit(ctx, " cmp	$0, %%eax\n");
	emit(ctx, " jne	%s\n", label_skip_2nd_clause);
	generate_expression(exp->u.bin_op.rexp, ctx, 1);
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

	generate_expression(exp->u.bin_op.lexp, ctx, 1);
	emit(ctx, " cmp	$0, %%eax\n");
	emit(ctx, " je	%s\n", label_skip_2nd_clause);
	generate_expression(exp->u.bin_op.rexp, ctx, 1);
	emit(ctx, " cmp	$0, %%eax\n");
	emit(ctx, "%s:\n", label_skip_2nd_clause);
	emit(ctx, " mov	$0, %%eax\n");
	emit(ctx, " setne	%%al\n");
	free(label_skip_2nd_clause);
}

static char *label_ternary_else(void)
{
	static unsigned long counter = 0;
	return xmkstr("_ternary_else_%lu", counter++);
}

static char *label_ternary_end(void)
{
	static unsigned long counter = 0;
	return xmkstr("_ternary_end_%lu", counter++);
}

static void generate_ternary(struct ast_expression *exp, struct x86_ctx *ctx,
			     int require_value)
{
	assert(exp->type == AST_EXP_TERNARY);
	char *label_else = label_ternary_else();
	char *label_end = label_ternary_end();

	generate_expression(exp->u.ternary.condition, ctx, 1);
	emit(ctx, " cmp	$0, %%eax\n");
	emit(ctx, " je	%s\n", label_else);
	generate_expression(exp->u.ternary.if_exp, ctx, require_value);
	emit(ctx, " jmp %s\n", label_end);
	emit(ctx, "%s:\n", label_else);
	generate_expression(exp->u.ternary.else_exp, ctx, require_value);
	emit(ctx, "%s:\n", label_end);

	free(label_else);
	free(label_end);
}

/* Convention: generate_expression should put the result in eax. */
static void generate_expression(struct ast_expression *exp, struct x86_ctx *ctx,
				int require_value)
{
	char *var_ref = NULL;
	switch (exp->type) {
	case AST_EXP_BINARY_OP:
		enum bin_op_type bin_op_type = exp->u.bin_op.type;

		/*
		 * We check these first because they have their own semantics
		 * regarding the calculation (or not) of the expresions.
		 */
		if (bin_op_type == EXP_OP_LOGIC_AND) {
			generate_logic_and(exp, ctx);
			goto out;
		} else if (bin_op_type == EXP_OP_LOGIC_OR) {
			generate_logic_or(exp, ctx);
			goto out;
		} else if (bin_op_type == EXP_OP_ASSIGNMENT) {
			struct ast_expression *lexp = exp->u.bin_op.lexp;
			assert(lexp->type == AST_EXP_VAR);
			var_ref = symtable_var_ref(ctx->symtable, &lexp->u.var);
			generate_expression(exp->u.bin_op.rexp, ctx, 1);
			emit(ctx, " movl	%%eax, %s\n", var_ref);
			goto out;
		}

		/*
		 * "sub ecx eax" does "eax = eax - ecx". We calculate
		 * rexp first so that its value ends up in ecx and lexp
		 * ends up in eax, making them ready for the subtraction. We
		 * could, instead, manipulate the registers after the
		 * sub-expression is calculated, but it is easier to do it this
		 * way.
		 */
		generate_expression(exp->u.bin_op.rexp, ctx, 1);
		/*
		 * Saving this in a register would be faster, but we don't know
		 * how many sub-expressions there is and register allocation is
		 * more complex than simplying pushing this value into the
		 * stack.
		 */
		emit(ctx, " push	%%rax\n");
		ctx->stack_index += 8;
		generate_expression(exp->u.bin_op.lexp, ctx, 1);
		emit(ctx, " pop	%%rcx\n");
		ctx->stack_index -= 8;

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

	case AST_EXP_TERNARY:
		generate_ternary(exp, ctx, require_value);
		break;

	case AST_EXP_UNARY_OP:
		struct ast_expression *un_op_val = exp->u.un_op.exp;
		generate_expression(un_op_val, ctx, 1);
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
			var_ref = symtable_var_ref(ctx->symtable, &un_op_val->u.var);
			emit(ctx, " add	$1, %%eax\n");
			emit(ctx, " movl	%%eax, %s\n", var_ref);
			break;
		case EXP_OP_PREFIX_DEC:
			assert(un_op_val->type == AST_EXP_VAR);
			var_ref = symtable_var_ref(ctx->symtable, &un_op_val->u.var);
			emit(ctx, " sub	$1, %%eax\n");
			emit(ctx, " movl	%%eax, %s\n", var_ref);
			break;
		case EXP_OP_SUFFIX_INC:
			assert(un_op_val->type == AST_EXP_VAR);
			var_ref = symtable_var_ref(ctx->symtable, &un_op_val->u.var);
			emit(ctx, " addl	$1, %s\n", var_ref);
			break;
		case EXP_OP_SUFFIX_DEC:
			assert(un_op_val->type == AST_EXP_VAR);
			var_ref = symtable_var_ref(ctx->symtable, &un_op_val->u.var);
			emit(ctx, " subl	$1, %s\n", var_ref);
			break;
		default:
			die("generate x86: unknown unary op: %d", exp->u.un_op.type);
		}
		break;
	case AST_EXP_CONSTANT_INT:
		emit(ctx, " mov	$%d, %%eax\n", exp->u.ival);
		break;
	case AST_EXP_VAR:
		var_ref = symtable_var_ref(ctx->symtable, &exp->u.var);
		emit(ctx, " movl	%s, %%eax\n", var_ref);
		break;
	case AST_EXP_FUNC_CALL:
		/*
		 * We first push all arguments into the stack and then move the
		 * register ones out. This is to avoid putting an argument in
		 * a register which may end up getting used by the next
		 * generate_expression() call.
		 *
		 * Note the ssize_t usage to avoid an unsigned overflow with
		 * i-- when i is 0.
		 */
		struct ast_func_decl *decl =
				symtable_func_call(ctx->symtable, &exp->u.call);

		if (require_value && decl->return_type == RET_VOID)
			die("void not ignored as it ought to be\n%s",
			    show_token_on_source_line(exp->u.call.tok));

		for (ssize_t i = exp->u.call.args.nr - 1; i >= 0; i--) {
			generate_expression(exp->u.call.args.arr[i], ctx, 1);
			emit(ctx, " push	%%rax\n");
			ctx->stack_index += 8;
		}
		for (size_t i = 0; i < MIN(NR_CALL_REGS, exp->u.call.args.nr); i++) {
			emit(ctx, " pop	%%%s\n", func_call_regs[i]);
			ctx->stack_index -= 8;
		}
		/* 
		 * No need to save any register as we hold all variables at
		 * the stack. So the callee can use all registers as it wants.
		 *
		 * TODO: check if we should align the stack before call.
		 */
		emit(ctx, " call	%s\n", exp->u.call.name);
		/* Remove the stack arguments. */
		size_t stack_args = exp->u.call.args.nr > NR_CALL_REGS ?
				    exp->u.call.args.nr - NR_CALL_REGS : 0;
		if (stack_args) {
			emit(ctx, " add	$%zu, %%rsp\n", stack_args * 4);
			ctx->stack_index -= (stack_args * 4);
		}
		break;
	default:
		die("generate x86: unknown expression type %d", exp->type);
	}
out:
	free(var_ref);
}

static void generate_func_epilogue_and_ret(struct x86_ctx *ctx)
{
	/* epilogue: restore previous stack frame. */
	emit(ctx, " mov	%%rbp, %%rsp\n");
	emit(ctx, " pop	%%rbp\n");
	emit(ctx, " ret\n");
}

static char *label_if_else_else(void)
{
	static unsigned long counter = 0;
	return xmkstr("_else_%lu", counter++);
}

static char *label_if_else_end(void)
{
	static unsigned long counter = 0;
	return xmkstr("_if_else_end_%lu", counter++);
}

static void generate_if_else(struct if_else *ie, struct x86_ctx *ctx)
{
	char *label_end = label_if_else_end();

	if (ie->else_st) {
		char *label_else = label_if_else_else();
		generate_expression(ie->condition, ctx, 1);
		emit(ctx, " cmp	$0, %%eax\n");
		emit(ctx, " je	%s\n", label_else);
		generate_statement(ie->if_st, ctx);
		emit(ctx, " jmp %s\n", label_end);
		emit(ctx, "%s:\n", label_else);
		generate_statement(ie->else_st, ctx);
		emit(ctx, "%s:\n", label_end);
		free(label_else);
	} else {
		generate_expression(ie->condition, ctx, 1);
		emit(ctx, " cmp	$0, %%eax\n");
		emit(ctx, " je	%s\n", label_end);
		generate_statement(ie->if_st, ctx);
		emit(ctx, "%s:\n", label_end);
	}

	free(label_end);
}

static void generate_new_scope(struct ast_statement *st, struct x86_ctx *ctx,
		void (*generator)(struct ast_statement *st, struct x86_ctx *ctx, void *data),
		void *data)
{
	struct symtable *cpy, *saved_symtable;
	unsigned int saved_scope = ctx->scope++;
	size_t saved_stack_index = ctx->stack_index;

	cpy = xmalloc(sizeof(*cpy));
	symtable_cpy(cpy, ctx->symtable);
	saved_symtable = ctx->symtable;
	ctx->symtable = cpy;

	generator(st, ctx, data);

	/* 
	 * Deallocate block variables. Alternatively, we could do:
	 * rsp = rbp - saved_stack_index;
	 */
	emit(ctx, " add	$%zu, %%rsp\n",
		symtable_bytes_in_scope(ctx->symtable, ctx->scope));

	ctx->scope = saved_scope;
	ctx->symtable = saved_symtable;
	ctx->stack_index = saved_stack_index;
	symtable_destroy(cpy);
	free(cpy);
}

static void block_statement_generator(struct ast_statement *st,
				      struct x86_ctx *ctx,
				      void *unused)
{
	assert(st->type == AST_ST_BLOCK);
	for (size_t i = 0; i < st->u.block.nr; i++)
		generate_statement(st->u.block.items[i], ctx);
}
#define generate_statement_block(st, ctx) \
	generate_new_scope(st, ctx, block_statement_generator, NULL)

static void generate_while(struct ast_statement *st, struct x86_ctx *ctx)
{
	static unsigned long counter = 0;
	char *label_start = xmkstr("_while_start_%lu", counter);
	char *label_end = xmkstr("_while_end_%lu", counter);
	counter++;
	stack_push(&ctx->break_labels, label_end);
	stack_push(&ctx->continue_labels, label_start);

	assert(st->type == AST_ST_WHILE);
	emit(ctx, "%s:\n", label_start);
	generate_expression(st->u._while.condition, ctx, 1);
	emit(ctx, " cmp	$0, %%eax\n");
	emit(ctx, " je	%s\n", label_end);
	generate_statement(st->u._while.body, ctx);
	emit(ctx, " jmp %s\n", label_start);
	emit(ctx, "%s:\n", label_end);

	stack_pop(&ctx->break_labels);
	stack_pop(&ctx->continue_labels);
	free(label_start);
	free(label_end);
}

static void generate_do(struct ast_statement *st, struct x86_ctx *ctx)
{
	static unsigned long counter = 0;
	char *label_start = xmkstr("_do_start_%lu", counter);
	char *label_end = xmkstr("_do_end_%lu", counter);
	char *label_condition = xmkstr("_do_condition_%lu", counter);
	counter++;
	stack_push(&ctx->break_labels, label_end);
	stack_push(&ctx->continue_labels, label_condition);

	assert(st->type == AST_ST_DO);
	emit(ctx, "%s:\n", label_start);
	generate_statement(st->u._do.body, ctx);
	emit(ctx, "%s:\n", label_condition);
	generate_expression(st->u._do.condition, ctx, 1);
	emit(ctx, " cmp	$0, %%eax\n");
	emit(ctx, " jne	%s\n", label_start);
	emit(ctx, "%s:\n", label_end);

	stack_pop(&ctx->break_labels);
	stack_pop(&ctx->continue_labels);
	free(label_start);
	free(label_end);
	free(label_condition);
}

static void generate_opt_expression(struct ast_opt_expression opt_exp,
				    struct x86_ctx *ctx,
				    int require_value)
{
	if (opt_exp.exp)
		generate_expression(opt_exp.exp, ctx, require_value);
}

static void generate_for(struct ast_statement *st, struct x86_ctx *ctx)
{
	static unsigned long counter = 0;
	char *label_condition = xmkstr("_for_condition_%lu", counter);
	char *label_end = xmkstr("_for_end_%lu", counter);
	char *label_epilogue = xmkstr("_for_epilogue_%lu", counter);
	counter++;
	stack_push(&ctx->break_labels, label_end);
	stack_push(&ctx->continue_labels, label_epilogue);

	assert(st->type == AST_ST_FOR);
	generate_opt_expression(st->u._for.prologue, ctx, 0);
	emit(ctx, "%s:\n", label_condition);
	generate_expression(st->u._for.condition, ctx, 1);
	emit(ctx, " cmp	$0, %%eax\n");
	emit(ctx, " je	%s\n", label_end);
	generate_statement(st->u._for.body, ctx);
	emit(ctx, "%s:\n", label_epilogue);
	generate_opt_expression(st->u._for.epilogue, ctx, 0);
	emit(ctx, " jmp	%s\n", label_condition);
	emit(ctx, "%s:\n", label_end);

	stack_pop(&ctx->break_labels);
	stack_pop(&ctx->continue_labels);
	free(label_condition);
	free(label_end);
	free(label_epilogue);
}

static void generate_var_decl(struct ast_var_decl *decl, struct x86_ctx *ctx)
{
	/*
	 * Put the varname on the symbol table before generating the
	 * code for rexp due to the weird case of declaration with
	 * assignment to itself:
	 *		int v = v = 2;
	 */
	size_t var_stack_index = ctx->stack_index += 4;
	symtable_put_lvar(ctx->symtable, decl, ctx->stack_index, ctx->scope);
	emit(ctx, " sub	$4, %%rsp\n");

	if (decl->value) {
		generate_expression(decl->value, ctx, 1);
	} else {
		/* We don't really need to initialize it, but... */
		emit(ctx, " mov	$0, %%eax\n");
	}
	emit(ctx, " movl	%%eax, -%zu(%%rbp)\n", var_stack_index);
}

static void for_decl_generator(struct ast_statement *st, struct x86_ctx *ctx,
			       void *unused)
{
	static unsigned long counter = 0;
	char *label_condition = xmkstr("_for_decl_condition_%lu", counter);
	char *label_end = xmkstr("_for_decl_end_%lu", counter);
	char *label_epilogue = xmkstr("_for_decl_epilogue_%lu", counter);
	counter++;
	stack_push(&ctx->break_labels, label_end);
	stack_push(&ctx->continue_labels, label_epilogue);

	assert(st->type == AST_ST_FOR_DECL);
	generate_var_decl(st->u.for_decl.decl, ctx);
	emit(ctx, "%s:\n", label_condition);
	generate_expression(st->u._for.condition, ctx, 1);
	emit(ctx, " cmp	$0, %%eax\n");
	emit(ctx, " je	%s\n", label_end);
	generate_statement(st->u._for.body, ctx);
	emit(ctx, "%s:\n", label_epilogue);
	generate_opt_expression(st->u._for.epilogue, ctx, 0);
	emit(ctx, " jmp	%s\n", label_condition);
	emit(ctx, "%s:\n", label_end);

	stack_pop(&ctx->break_labels);
	stack_pop(&ctx->continue_labels);
	free(label_condition);
	free(label_end);
	free(label_epilogue);
}
#define generate_for_decl(st, ctx) \
	generate_new_scope(st, ctx, for_decl_generator, NULL)

static void generate_statement(struct ast_statement *st, struct x86_ctx *ctx)
{
	const char *label;
	struct token *tok;
	switch(st->type) {
	case AST_ST_RETURN:
		if (st->u._return.opt_exp.exp) {
			if (ctx->cur_func->return_type != RET_INT) {
				die("trying to return value from void function\n%s\nFunction declared at:\n%s",
				    show_token_on_source_line(st->u._return.tok),
				    show_token_on_source_line(ctx->cur_func->tok));
			}
			generate_expression(st->u._return.opt_exp.exp, ctx, 1);
		} else if (ctx->cur_func->return_type != RET_VOID) {
			die("missing return value on non-void function\n%s\nFunction declared at:\n%s",
			    show_token_on_source_line(st->u._return.tok),
			    show_token_on_source_line(ctx->cur_func->tok));
		}
		generate_func_epilogue_and_ret(ctx);
		break;
	case AST_ST_VAR_DECL:
		generate_var_decl(st->u.decl, ctx);
		break;
	case AST_ST_EXPRESSION:
		generate_opt_expression(st->u.opt_exp, ctx, 0);
		break;
	case AST_ST_IF_ELSE:
		generate_if_else(&st->u.if_else, ctx);
		break;
	case AST_ST_BLOCK:
		generate_statement_block(st, ctx);
		break;
	case AST_ST_WHILE:
		generate_while(st, ctx);
		break;
	case AST_ST_DO:
		generate_do(st, ctx);
		break;
	case AST_ST_FOR:
		generate_for(st, ctx);
		break;
	case AST_ST_FOR_DECL:
		generate_for_decl(st, ctx);
		break;
	case AST_ST_BREAK:
		if (stack_empty(&ctx->break_labels))
			die("generate x86: nothing to break from.\n%s",
			    show_token_on_source_line(st->u.break_tok));
		emit(ctx, " jmp  %s\n", (char *)stack_peek(&ctx->break_labels));
		break;
	case AST_ST_CONTINUE:
		if (stack_empty(&ctx->continue_labels))
			die("generate x86: nothing to continue to.\n%s",
			    show_token_on_source_line(st->u.continue_tok));
		emit(ctx, " jmp  %s\n", (char *)stack_peek(&ctx->continue_labels));
		break;
	case AST_ST_LABELED_STATEMENT:
		label = st->u.labeled_st.label;
		tok = st->u.labeled_st.label_tok;
		labelset_put_definition(&ctx->user_labels, label, tok);
		emit(ctx, "_label_%s:\n", label);
		generate_statement(st->u.labeled_st.st, ctx);
		break;
	case AST_ST_GOTO:
		label = st->u._goto.label;
		tok = st->u._goto.label_tok;
		labelset_put_reference(&ctx->user_labels, label, tok);
		emit(ctx, " jmp _label_%s\n", label);
		break;

	default:
		die("generate x86: unknown statement type %d", st->type);
	}
}

static void func_body_generator(struct ast_statement *st,
				struct x86_ctx *ctx,
				void *data)
{
	ARRAY(struct ast_var_decl *) *parameters = data;
	/* First we save the arguments. */
	for (size_t i = 0; i < parameters->nr; i++) {
		if (i < NR_CALL_REGS) {
			/* 
			 * TODO: there is no need for using rax as a temporary
			 * register if we directly use the 32-bits part of
			 * func_call_regs[i].
			 */
			emit(ctx, " mov	%%%s, %%rax\n", func_call_regs[i]);
			emit(ctx, " sub	$4, %%rsp\n");
			emit(ctx, " movl	%%eax, (%%rsp)\n");
		} else {
			/*
			 * The NR_CALL_REGS-th argument is 16 positions above
			 * rbp: 8 bytes are used for the return address and
			 * another 8 to save the previous rbp (function
			 * prologue). Subsequent ones are above it (remember:
			 * the stack grows down, i.e., to lower addresses).
			 */
			emit(ctx, " mov	%zu(%%rbp), %%rax\n",
			     16 + (i - NR_CALL_REGS) * 8);
			emit(ctx, " sub	$4, %%rsp\n");
			emit(ctx, " movl	%%eax, (%%rsp)\n");
		}
		ctx->stack_index += 4;
		symtable_put_lvar(ctx->symtable, parameters->arr[i],
				  ctx->stack_index, ctx->scope);
	}

	/* Then we generate the body. */
	assert(st->type == AST_ST_BLOCK);
	for (size_t i = 0; i < st->u.block.nr; i++)
		generate_statement(st->u.block.items[i], ctx);
}
#define generate_func_body(func, ctx) \
	generate_new_scope((func)->body, ctx, func_body_generator, &(func)->parameters)

static void generate_func_decl(struct ast_func_decl *fun, struct x86_ctx *ctx)
{
	symtable_put_func(ctx->symtable, fun, ctx->scope);
	if (!fun->body)
		return;

	ctx->cur_func = fun;
	labelset_init(&ctx->user_labels);
	emit(ctx, " .text\n");
	emit(ctx, " .globl %s\n", fun->name);
	emit(ctx, "%s:\n", fun->name);

	/*
	 * prologue: save previous rbp and create an empty stack frame.
	 * Note: callee should also save and restore RBX and R12-R15, but we
	 * never use these registers, so there is no need to save them. 
	 */
	emit(ctx, " push	%%rbp\n");
	emit(ctx, " mov	%%rsp, %%rbp\n");
	ctx->stack_index = 0;

	generate_func_body(fun, ctx);
	/*
	 * If the function is missing a return statement, and it is
	 * the main() function, it should return 0. If it is not
	 * main() and its type is not void, the behavior is undefined. To
	 * keep uniformity, we will return 0 in both cases.
	 *
	 * NEEDSWORK: this will be redundant if there was already a return
	 * statement, but it would be trickier to test whether all if-else
	 * branches have return statements, so we accept the
	 * redundancy.
	 */
	if (!strcmp(fun->name, "main") || fun->return_type != RET_VOID)
		emit(ctx, " mov	$0, %%eax\n");
	generate_func_epilogue_and_ret(ctx);

	assert(stack_empty(&ctx->continue_labels) && stack_empty(&ctx->break_labels));
	stack_destroy(&ctx->continue_labels, NULL);
	stack_destroy(&ctx->break_labels, NULL);

	/* Check if all refered labels were defined. */
	labelset_check(&ctx->user_labels);
	labelset_destroy(&ctx->user_labels);
	ctx->cur_func = NULL;
}

static void generate_global_var_decl(struct ast_var_decl *var,
				     struct x86_ctx *ctx)
{
	char *var_label = symtable_put_gvar(ctx->symtable, var);
	if (var->value) {
		/*
		 * The parser should have already verified this and warned the
		 * user.
		 */
		assert(var->value->type == AST_EXP_CONSTANT_INT);
		emit(ctx, " .data\n");
		emit(ctx, " .globl %s\n", var_label);
		emit(ctx, " .align 4\n");
		emit(ctx, "%s:\n", var_label);
		emit(ctx, " .long %d\n", var->value->u.ival);
	} else {
		/* Uninitialized global vars will be generated at the end */
	}
	free(var_label);
}

static void generate_uninitialized_gvar(char *var_label, void *data)
{
	struct x86_ctx *ctx = data;
	emit(ctx, " .bss\n");
	emit(ctx, " .globl %s\n", var_label);
	emit(ctx, " .align 4\n");
	emit(ctx, "%s:\n", var_label);
	emit(ctx, " .zero 4\n");
}
#define generate_uninitialized_gvars(ctx) \
	foreach_uninitialized_gvar(ctx->symtable, generate_uninitialized_gvar, ctx);

static void generate_prog(struct ast_program *prog, struct x86_ctx *ctx)
{
	for (size_t i = 0; i < prog->items.nr; i++) {
		struct ast_toplevel_item *item = prog->items.arr[i];
		switch (item->type) {
		case TOPLEVEL_FUNC_DECL:
			generate_func_decl(item->u.func, ctx);
			break;
		case TOPLEVEL_VAR_DECL:
			generate_global_var_decl(item->u.var, ctx);
			break;
		default:
			BUG("x86: unknown toplevel item '%s'", item->type);
		}
	}
	generate_uninitialized_gvars(ctx);
}

void generate_x86_asm(struct ast_program *prog, FILE *out)
{
	struct x86_ctx ctx = { 0 };
	struct symtable symtable;

	symtable_init(&symtable);
	ctx.symtable = &symtable;
	ctx.out = out;
	generate_prog(prog, &ctx);
	fflush(out);

	symtable_destroy(&symtable);
}
