#include "util.h"
#include "lexer.h"
#include "parser.h"
#include "lib/array.h"

/*******************************************************************************
 *				Parsing
*******************************************************************************/

static struct ast_expression *parse_exp(struct token **tok_ptr);
static struct ast_expression *parse_exp_no_comma(struct token **tok_ptr);
static struct ast_statement *parse_statement(struct token **tok_ptr);
static struct ast_statement *parse_statement_1(struct token **tok_ptr,
					       int allow_declaration);
static void free_ast_var_decl_list(struct ast_var_decl_list *decl_list);

static char *str_join_token_types(const char *clause, va_list tt_list)
{
	char *joined = NULL;
	enum token_type type = va_arg(tt_list, enum token_type);

	/* TODO: another excellent use case for strbuf. */
	while (type != TOK_NONE) {
		enum token_type next_type = va_arg(tt_list, enum token_type);
		const char *type_str = tt2str(type);
		if (!joined) {
			joined = xstrdup(type_str);
		} else {
			char *to_free = joined;
			if (next_type == TOK_NONE)
				joined = xmkstr("%s, %s %s", joined, clause, type_str);
			else
				joined = xmkstr("%s, %s", joined, type_str);
			free(to_free);
		}
		type = next_type;
	}

	va_end(tt_list);
	return joined;
}

/* Don't use this directly, use check_and_pop() instead. */
static enum token_type check_and_pop_1(struct token **tok_ptr, int abort_on_miss, ...)
{
	va_list args;
	va_start(args, abort_on_miss);
	for (enum token_type etype = va_arg(args, enum token_type);
	     etype != TOK_NONE;
	     etype = va_arg(args, enum token_type)) {
		if ((*tok_ptr)->type == etype) {
			va_end(args);
			(*tok_ptr)++;
			return etype;
		}
	}
	va_end(args);

	if (abort_on_miss) {
		va_start(args, abort_on_miss);
		die("parser: expecting %s got %s\n%s", \
		    str_join_token_types("or", args), tok2str(*tok_ptr), \
				    show_token_on_source_line(*tok_ptr)); \
		va_end(args);
	}

	return 0; /* TOK_NONE */
}

#define check_and_pop(tok_ptr, ...) \
	check_and_pop_1(tok_ptr, 1, __VA_ARGS__, TOK_NONE)

#define check_and_pop_gently(tok_ptr, ...) \
	check_and_pop_1(tok_ptr, 0, __VA_ARGS__, TOK_NONE)

static enum un_op_type tt2un_op_type(enum token_type type)
{
	switch (type) {
	case TOK_MINUS: return EXP_OP_NEGATION;
	case TOK_TILDE: return EXP_OP_BIT_COMPLEMENT;
	case TOK_LOGIC_NOT: return EXP_OP_LOGIC_NEGATION;
	default: die("BUG: unknown token type at tt2un_op_type: %d", type);
	}
}

static struct ast_expression *parse_exp_atom(struct token **tok_ptr)
{
	struct ast_expression *exp;
	struct token *tok = *tok_ptr;

	check_and_pop(&tok, TOK_INTEGER, TOK_OPEN_PAR, TOK_MINUS, TOK_TILDE,
		      TOK_LOGIC_NOT, TOK_PLUS, TOK_IDENTIFIER,
		      TOK_PLUS_PLUS, TOK_MINUS_MINUS);

	if (tok[-1].type == TOK_INTEGER) {
		exp = xmalloc(sizeof(*exp));
		exp->type = AST_EXP_CONSTANT_INT;
		exp->u.ival = *((int *)tok[-1].value);
	} else if (tok[-1].type == TOK_OPEN_PAR) {
		exp = parse_exp(&tok);
		check_and_pop(&tok, TOK_CLOSE_PAR);
	} else if (tok[-1].type == TOK_IDENTIFIER && check_and_pop_gently(&tok, TOK_OPEN_PAR)) {
		exp = xmalloc(sizeof(*exp));
		exp->type = AST_EXP_FUNC_CALL;
		exp->u.call.name = xstrdup((char *)tok[-2].value);
		exp->u.call.tok = &tok[-2];
		ARRAY_INIT(&exp->u.call.args);
		int is_first_parameter = 1;
		while (tok->type != TOK_CLOSE_PAR) {
			if (!is_first_parameter)
				check_and_pop(&tok, TOK_COMMA);
			ARRAY_APPEND(&exp->u.call.args, parse_exp_no_comma(&tok));
			is_first_parameter = 0;
		}
		check_and_pop(&tok, TOK_CLOSE_PAR);
	} else if (tok[-1].type == TOK_IDENTIFIER) {
		exp = xmalloc(sizeof(*exp));
		exp->type = AST_EXP_VAR;
		exp->u.var.name = xstrdup((char *)tok[-1].value);
		exp->u.var.tok = &tok[-1];
	} else if (tok[-1].type == TOK_PLUS) {
		/* This unary operator does nothing, so we just remove it. */
		exp = parse_exp_atom(&tok);
	} else if (tok[-1].type == TOK_PLUS_PLUS || tok[-1].type == TOK_MINUS_MINUS) {
		struct token *op_tok = &tok[-1];
		exp = xmalloc(sizeof(*exp));
		exp->type = AST_EXP_UNARY_OP;
		exp->u.un_op.exp = parse_exp(&tok);
		if (exp->u.un_op.exp->type != AST_EXP_VAR)
			die("parser: preffix inc/dec operators require an lvalue on the right.\n%s",
			    show_token_on_source_line(op_tok));
		exp->u.un_op.type = op_tok->type == TOK_PLUS_PLUS ?
			EXP_OP_PREFIX_INC : EXP_OP_PREFIX_DEC;
	} else {
		exp = xmalloc(sizeof(*exp));
		exp->type = AST_EXP_UNARY_OP;
		exp->u.un_op.type = tt2un_op_type(tok[-1].type);
		exp->u.un_op.exp = parse_exp_atom(&tok);
	}

	enum token_type suffix =
		check_and_pop_gently(&tok, TOK_PLUS_PLUS, TOK_MINUS_MINUS);
	if (suffix) {
		struct token *op_tok = &tok[-1];
		if (exp->type != AST_EXP_VAR)
			die("parser: suffix inc/dec operators require an lvalue on the left.\n%s",
			    show_token_on_source_line(op_tok));
		struct ast_expression *suffix_exp = xmalloc(sizeof(*suffix_exp));
		suffix_exp->type = AST_EXP_UNARY_OP;
		suffix_exp->u.un_op.exp = exp;
		suffix_exp->u.un_op.type = op_tok->type == TOK_PLUS_PLUS ?
			EXP_OP_SUFFIX_INC : EXP_OP_SUFFIX_DEC;
		exp = suffix_exp;
	}

	*tok_ptr = tok;
	return exp;
}

struct bin_op_info {
	enum associativity { ASSOC_UNKNOWN=0, ASSOC_LEFT, ASSOC_RIGHT } assoc;
	unsigned int precedence;
};

/*
 * Source: https://en.cppreference.com/w/c/language/operator_precedence. But we
 * invert the number sequence, so the lowest value has the highest precedence.
 */
static struct bin_op_info bin_op_info[] = {
	[EXP_OP_COMMA] =                { .assoc=ASSOC_LEFT,  .precedence=1 },
	[EXP_OP_ASSIGNMENT] =           { .assoc=ASSOC_RIGHT, .precedence=2 },
	[EXP_OP_LOGIC_OR] =             { .assoc=ASSOC_LEFT,  .precedence=4 },
	[EXP_OP_LOGIC_AND] =            { .assoc=ASSOC_LEFT,  .precedence=5 },
	[EXP_OP_BITWISE_OR] =           { .assoc=ASSOC_LEFT,  .precedence=6 },
	[EXP_OP_BITWISE_XOR] =          { .assoc=ASSOC_LEFT,  .precedence=7 },
	[EXP_OP_BITWISE_AND] =          { .assoc=ASSOC_LEFT,  .precedence=8 },
	[EXP_OP_EQUAL] =                { .assoc=ASSOC_LEFT,  .precedence=9 },
	[EXP_OP_NOT_EQUAL] =            { .assoc=ASSOC_LEFT,  .precedence=9 },
	[EXP_OP_LT] =                   { .assoc=ASSOC_LEFT,  .precedence=10 },
	[EXP_OP_LE] =                   { .assoc=ASSOC_LEFT,  .precedence=10 },
	[EXP_OP_GT] =                   { .assoc=ASSOC_LEFT,  .precedence=10 },
	[EXP_OP_GE] =                   { .assoc=ASSOC_LEFT,  .precedence=10 },
	[EXP_OP_BITWISE_LEFT_SHIFT] =   { .assoc=ASSOC_LEFT,  .precedence=11 },
	[EXP_OP_BITWISE_RIGHT_SHIFT] =  { .assoc=ASSOC_LEFT,  .precedence=11 },
	[EXP_OP_SUBTRACTION] =          { .assoc=ASSOC_LEFT,  .precedence=12 },
	[EXP_OP_ADDITION] =             { .assoc=ASSOC_LEFT,  .precedence=12 },
	[EXP_OP_MULTIPLICATION] =       { .assoc=ASSOC_LEFT,  .precedence=13 },
	[EXP_OP_DIVISION] =             { .assoc=ASSOC_LEFT,  .precedence=13 },
	[EXP_OP_MODULO] =               { .assoc=ASSOC_LEFT,  .precedence=13 },
	[EXP_BIN_OP_NR] =               { 0 },
};

static int is_bin_op_tok_1(enum token_type type, enum bin_op_type *ret)
{
	switch (type) {
	case TOK_MINUS:               *ret = EXP_OP_SUBTRACTION; return 1;
	case TOK_PLUS:                *ret = EXP_OP_ADDITION; return 1;
	case TOK_STAR:                *ret = EXP_OP_MULTIPLICATION; return 1;
	case TOK_F_SLASH:             *ret = EXP_OP_DIVISION; return 1;
	case TOK_MODULO:              *ret = EXP_OP_MODULO; return 1;
	case TOK_LOGIC_AND:           *ret = EXP_OP_LOGIC_AND; return 1;
	case TOK_LOGIC_OR:            *ret = EXP_OP_LOGIC_OR; return 1;
	case TOK_EQUAL:               *ret = EXP_OP_EQUAL; return 1;
	case TOK_NOT_EQUAL:           *ret = EXP_OP_NOT_EQUAL; return 1;
	case TOK_LT:                  *ret = EXP_OP_LT; return 1;
	case TOK_LE:                  *ret = EXP_OP_LE; return 1;
	case TOK_GT:                  *ret = EXP_OP_GT; return 1;
	case TOK_GE:                  *ret = EXP_OP_GE; return 1;
	case TOK_BITWISE_AND:         *ret = EXP_OP_BITWISE_AND; return 1;
	case TOK_BITWISE_OR:          *ret = EXP_OP_BITWISE_OR; return 1;
	case TOK_BITWISE_XOR:         *ret = EXP_OP_BITWISE_XOR; return 1;
	case TOK_BITWISE_LEFT_SHIFT:  *ret = EXP_OP_BITWISE_LEFT_SHIFT; return 1;
	case TOK_BITWISE_RIGHT_SHIFT: *ret = EXP_OP_BITWISE_RIGHT_SHIFT; return 1;
	case TOK_COMMA:               *ret = EXP_OP_COMMA; return 1;
	case TOK_ASSIGNMENT:
	case TOK_PLUS_ASSIGNMENT:
	case TOK_MINUS_ASSIGNMENT:
	case TOK_SLASH_ASSIGNMENT:
	case TOK_STAR_ASSIGNMENT:
	case TOK_MODULO_ASSIGNMENT:
	case TOK_BITWISE_AND_ASSIGNMENT:
	case TOK_BITWISE_OR_ASSIGNMENT:
	case TOK_BITWISE_XOR_ASSIGNMENT:
	case TOK_BITWISE_LEFT_SHIFT_ASSIGNMENT:
	case TOK_BITWISE_RIGHT_SHIFT_ASSIGNMENT:
				      *ret = EXP_OP_ASSIGNMENT; return 1;
	default:
				      return 0;
	}
}

static inline int is_bin_op_tok(enum token_type tt)
{
	enum bin_op_type unused;
	return is_bin_op_tok_1(tt, &unused);
}

static inline enum bin_op_type tt2bin_op_type(enum token_type tt)
{
	enum bin_op_type bin_type;
	if (!is_bin_op_tok_1(tt, &bin_type))
		die("BUG: tt2bin_op_type called for token that is not"
		    " a binary operation: %d\n", tt);
	return bin_type;
}

static int is_compound_assign(enum token_type tt, enum bin_op_type *compound_op)
{
	switch (tt) {
	case TOK_PLUS_ASSIGNMENT:
		*compound_op = EXP_OP_ADDITION; return 1;
	case TOK_MINUS_ASSIGNMENT:
		*compound_op = EXP_OP_SUBTRACTION; return 1;
	case TOK_SLASH_ASSIGNMENT:
		*compound_op = EXP_OP_DIVISION; return 1;
	case TOK_STAR_ASSIGNMENT:
		*compound_op = EXP_OP_MULTIPLICATION; return 1;
	case TOK_MODULO_ASSIGNMENT:
		*compound_op = EXP_OP_MODULO; return 1;
	case TOK_BITWISE_AND_ASSIGNMENT:
		*compound_op = EXP_OP_BITWISE_AND; return 1;
	case TOK_BITWISE_OR_ASSIGNMENT:
		*compound_op = EXP_OP_BITWISE_OR; return 1;
	case TOK_BITWISE_XOR_ASSIGNMENT:
		*compound_op = EXP_OP_BITWISE_XOR; return 1;
	case TOK_BITWISE_LEFT_SHIFT_ASSIGNMENT:
		*compound_op = EXP_OP_BITWISE_LEFT_SHIFT; return 1;
	case TOK_BITWISE_RIGHT_SHIFT_ASSIGNMENT:
		*compound_op = EXP_OP_BITWISE_RIGHT_SHIFT; return 1;
	default:
		return 0;
	}
}

static unsigned int bin_op_precedence(enum bin_op_type type)
{
	assert(type >= 0 && type < EXP_BIN_OP_NR);
	if (!bin_op_info[type].precedence)
		die("BUG: we don't have precedence info for token %d\n"
		    "Did you forget to update the bin_op_info array?", type);
	return bin_op_info[type].precedence;
}

static enum associativity bin_op_associativity(enum bin_op_type type)
{
	assert(type >= 0 && type < EXP_BIN_OP_NR);
	if (!bin_op_info[type].assoc)
		die("BUG: we don't have associativity info for token %d\n"
		    "Did you forget to update the bin_op_info array?", type);
	return bin_op_info[type].assoc;
}

struct ast_expression *ast_expression_var_dup(struct ast_expression *vexp)
{
	assert(vexp->type == AST_EXP_VAR);
	struct ast_expression *cpy = xmalloc(sizeof(*cpy));
	cpy->type = AST_EXP_VAR;
	cpy->u.var.name = xstrdup(vexp->u.var.name);
	cpy->u.var.tok = vexp->u.var.tok;
	return cpy;
}

#define is_ternary_op_tok(tt) ((tt) == TOK_QUESTION_MARK)

/* 
 * Parse expression using precedence climbing.
 * See: https://eli.thegreenplace.net/2012/08/02/parsing-expressions-by-precedence-climbing.
 */
static struct ast_expression *parse_exp_1(struct token **tok_ptr,
					  int allow_comma, int min_prec)
{
	struct token *op_tok, *tok = *tok_ptr;
	struct ast_expression *exp = parse_exp_atom(&tok);

	while (is_bin_op_tok(tok->type) || is_ternary_op_tok(tok->type)) {

		if (is_ternary_op_tok(tok->type)) {
			const int ternary_prec = 3;
			const enum associativity ternary_assoc = ASSOC_RIGHT;

			if (ternary_prec < min_prec)
				break;
			tok++;

			struct ast_expression *condition = exp;
			exp = xmalloc(sizeof(*exp));
			exp->type = AST_EXP_TERNARY;
			exp->u.ternary.condition = condition;
			exp->u.ternary.if_exp = allow_comma ? parse_exp(&tok) :
						parse_exp_no_comma(&tok);
			check_and_pop(&tok, TOK_COLON);
			exp->u.ternary.else_exp = parse_exp_1(&tok, allow_comma,
					ternary_assoc == ASSOC_LEFT ?
					ternary_prec + 1 : ternary_prec);
			continue;
		}

		enum bin_op_type compound_op;
		enum bin_op_type bin_op_type = tt2bin_op_type(tok->type);
		int prec = bin_op_precedence(bin_op_type);

		if (!allow_comma && bin_op_type == EXP_OP_COMMA)
			break;

		if (bin_op_type == EXP_OP_ASSIGNMENT && exp->type != AST_EXP_VAR)
			die("parser: assignment operator requires lvalue on left side.\n%s",
			    show_token_on_source_line(tok));

		if (prec < min_prec)
			break;
		op_tok = tok;
		tok++;

		enum associativity assoc = bin_op_associativity(bin_op_type);

		struct ast_expression *lexp = exp;
		struct ast_expression *rexp =
			parse_exp_1(&tok, allow_comma,
				    assoc == ASSOC_LEFT ? prec + 1 : prec);

		exp = xmalloc(sizeof(*exp));
		exp->type = AST_EXP_BINARY_OP;
		exp->u.bin_op.type = bin_op_type;
		exp->u.bin_op.lexp = lexp;

		if (is_compound_assign(op_tok->type, &compound_op)) {
			struct ast_expression *compound_exp = xmalloc(sizeof(*compound_exp));
			compound_exp->type = AST_EXP_BINARY_OP;
			compound_exp->u.bin_op.type = compound_op;
			compound_exp->u.bin_op.lexp = ast_expression_var_dup(lexp);
			compound_exp->u.bin_op.rexp = rexp;

			exp->u.bin_op.rexp = compound_exp;
		} else {
			exp->u.bin_op.rexp = rexp;
		}
	}

	*tok_ptr = tok;
	return exp;
}

static struct ast_expression *parse_exp(struct token **tok_ptr)
{
	return parse_exp_1(tok_ptr, 1, 1);
}

static struct ast_expression *parse_exp_no_comma(struct token **tok_ptr)
{
	return parse_exp_1(tok_ptr, 0, 1);
}

static struct ast_var_decl_list *parse_var_decl_list(struct token **tok_ptr)
{
	struct ast_var_decl_list *decl_list = xcalloc(1, sizeof(*decl_list));
	struct token *tok = *tok_ptr;

	check_and_pop(&tok, TOK_INT_KW);
	do {
		struct ast_var_decl *decl = xcalloc(1, sizeof(*decl));
		check_and_pop(&tok, TOK_IDENTIFIER);
		decl->name = xstrdup((char *)tok[-1].value);
		decl->tok = &tok[-1];
		if (check_and_pop_gently(&tok, TOK_ASSIGNMENT))
			decl->value = parse_exp_no_comma(&tok);
		ARRAY_APPEND(decl_list, decl);
	} while (check_and_pop_gently(&tok, TOK_COMMA));

	*tok_ptr = tok;
	return decl_list;
}

static struct ast_statement *parse_statement_block(struct token **tok_ptr)
{
	struct ast_statement *st = xcalloc(1, sizeof(*st));
	struct token *tok = *tok_ptr;
	struct block *blk = &(st->u.block);

	check_and_pop(&tok, TOK_OPEN_BRACE);
	st->type = AST_ST_BLOCK;
	while (!end_token(tok) && tok->type != TOK_CLOSE_BRACE) {
		ALLOC_GROW(blk->items, blk->nr + 1, blk->alloc);
		blk->items[blk->nr++] = parse_statement(&tok);
	}
	check_and_pop(&tok, TOK_CLOSE_BRACE);

	*tok_ptr = tok;
	return st;
}

static struct ast_expression *gen_true_exp(void)
{
	struct ast_expression *exp = xmalloc(sizeof(*exp));
	exp->type = AST_EXP_CONSTANT_INT;
	exp->u.ival = 1;
	return exp;
}

static struct ast_statement *parse_for_statement(struct token **tok_ptr)
{
	struct token *tok = *tok_ptr;
	struct ast_statement *st = xmalloc(sizeof(*st));

	check_and_pop(&tok, TOK_FOR_KW);
	check_and_pop(&tok, TOK_OPEN_PAR);
	if (tok->type == TOK_INT_KW) {
		st->type = AST_ST_FOR_DECL;
		st->u.for_decl.decl_list = parse_var_decl_list(&tok);
		check_and_pop(&tok, TOK_SEMICOLON);
		if (check_and_pop_gently(&tok, TOK_SEMICOLON)) {
			st->u.for_decl.condition = gen_true_exp();
		} else {
			st->u.for_decl.condition = parse_exp(&tok);
			check_and_pop(&tok, TOK_SEMICOLON);
		}
		if (check_and_pop_gently(&tok, TOK_CLOSE_PAR)) {
			st->u.for_decl.epilogue.exp = NULL;
		} else {
			st->u.for_decl.epilogue.exp = parse_exp(&tok);
			check_and_pop(&tok, TOK_CLOSE_PAR);
		}
		st->u.for_decl.body = parse_statement_1(&tok, 0);
	} else {
		st->type = AST_ST_FOR;
		if (check_and_pop_gently(&tok, TOK_SEMICOLON)) {
			st->u._for.prologue.exp = NULL;
		} else {
			st->u._for.prologue.exp = parse_exp(&tok);
			check_and_pop(&tok, TOK_SEMICOLON);
		}
		if (check_and_pop_gently(&tok, TOK_SEMICOLON)) {
			st->u._for.condition = gen_true_exp();
		} else {
			st->u._for.condition = parse_exp(&tok);
			check_and_pop(&tok, TOK_SEMICOLON);
		}
		if (check_and_pop_gently(&tok, TOK_CLOSE_PAR)) {
			st->u._for.epilogue.exp = NULL;
		} else {
			st->u._for.epilogue.exp = parse_exp(&tok);
			check_and_pop(&tok, TOK_CLOSE_PAR);
		}
		st->u._for.body = parse_statement_1(&tok, 0);
	}

	*tok_ptr = tok;
	return st;
}

static struct ast_statement *parse_statement_1(struct token **tok_ptr,
					       int allow_declaration)
{
	struct ast_statement *st;
	struct token *tok = *tok_ptr;

	if (tok->type == TOK_OPEN_BRACE) {
		st = parse_statement_block(&tok);
		goto out;
	}

	if (tok->type == TOK_FOR_KW) {
		st = parse_for_statement(&tok);
		goto out;
	}

	st = xmalloc(sizeof(*st));

	if (check_and_pop_gently(&tok, TOK_RETURN_KW)) {
		st->type = AST_ST_RETURN;
		st->u._return.tok = &tok[-1];
		if (check_and_pop_gently(&tok, TOK_SEMICOLON)) {
			st->u._return.opt_exp.exp = NULL;
		} else {
			st->u._return.opt_exp.exp = parse_exp(&tok);
			check_and_pop(&tok, TOK_SEMICOLON);
		}

	} else if (check_and_pop_gently(&tok, TOK_IF_KW)) {
		st->type = AST_ST_IF_ELSE;
		check_and_pop(&tok, TOK_OPEN_PAR);
		st->u.if_else.condition = parse_exp(&tok);
		check_and_pop(&tok, TOK_CLOSE_PAR);
		/*
		 * NEEDSWORK: hacky, should probably introduce the
		 * struct ast_block_item type, which can be either a statement
		 * or a variable declaration, and leave declaration outside
		 * of the struct ast_statement definition.
		 */
		st->u.if_else.if_st = parse_statement_1(&tok, 0);
		if (check_and_pop_gently(&tok, TOK_ELSE_KW))
			st->u.if_else.else_st = parse_statement_1(&tok, 0);
		else
			st->u.if_else.else_st = NULL;

	} else if (allow_declaration && tok->type == TOK_INT_KW) {
		st->type = AST_ST_VAR_DECL;
		st->u.decl_list = parse_var_decl_list(&tok);
		check_and_pop(&tok, TOK_SEMICOLON);

	} else if (check_and_pop_gently(&tok, TOK_WHILE_KW)) {
		st->type = AST_ST_WHILE;
		check_and_pop(&tok, TOK_OPEN_PAR);
		st->u._while.condition = parse_exp(&tok);
		check_and_pop(&tok, TOK_CLOSE_PAR);
		st->u._while.body = parse_statement_1(&tok, 0);

	} else if (check_and_pop_gently(&tok, TOK_DO_KW)) {
		st->type = AST_ST_DO;
		st->u._do.body = parse_statement_1(&tok, 0);
		check_and_pop(&tok, TOK_WHILE_KW);
		check_and_pop(&tok, TOK_OPEN_PAR);
		st->u._do.condition = parse_exp(&tok);
		check_and_pop(&tok, TOK_CLOSE_PAR);
		check_and_pop(&tok, TOK_SEMICOLON);

	} else if (check_and_pop_gently(&tok, TOK_BREAK_KW)) {
		st->type = AST_ST_BREAK;
		st->u.break_tok = &tok[-1];
		check_and_pop(&tok, TOK_SEMICOLON);

	} else if (check_and_pop_gently(&tok, TOK_CONTINUE_KW)) {
		st->type = AST_ST_CONTINUE;
		st->u.continue_tok = &tok[-1];
		check_and_pop(&tok, TOK_SEMICOLON);

	} else if (check_and_pop_gently(&tok, TOK_GOTO_KW)) {
		st->type = AST_ST_GOTO;
		check_and_pop(&tok, TOK_IDENTIFIER);
		st->u._goto.label_tok = &tok[-1];
		st->u._goto.label = (const char *)tok[-1].value;
		check_and_pop(&tok, TOK_SEMICOLON);

	} else if (tok[0].type == TOK_IDENTIFIER && tok[1].type == TOK_COLON) {
		tok += 2;
		st->type = AST_ST_LABELED_STATEMENT;
		st->u.labeled_st.label = (const char *)tok[-2].value;
		st->u.labeled_st.label_tok = &tok[-2];
		st->u.labeled_st.st = parse_statement(&tok);

	} else if (check_and_pop_gently(&tok, TOK_SEMICOLON)) {
		st->type = AST_ST_EXPRESSION;
		st->u.opt_exp.exp = NULL;

	} else {
		/* must be an expression */
		st->type = AST_ST_EXPRESSION;
		st->u.opt_exp.exp = parse_exp(&tok);
		check_and_pop(&tok, TOK_SEMICOLON);
	}

out:
	*tok_ptr = tok;
	return st;
}

static struct ast_statement *parse_statement(struct token **tok_ptr)
{
	return parse_statement_1(tok_ptr, 1);
}

static struct ast_func_decl *parse_func_decl(struct token **tok_ptr)
{
	struct ast_func_decl *fun = xmalloc(sizeof(*fun));
	struct token *tok = *tok_ptr;

	switch (check_and_pop(&tok, TOK_INT_KW, TOK_VOID_KW)) {
	case TOK_INT_KW: fun->return_type = RET_INT; break;
	case TOK_VOID_KW: fun->return_type = RET_VOID; break;
	default: BUG("unexpected token type");
	}

	check_and_pop(&tok, TOK_IDENTIFIER);
	fun->name = xstrdup((char *)tok[-1].value);
	fun->tok = &tok[-1];
	ARRAY_INIT(&fun->parameters);

	check_and_pop(&tok, TOK_OPEN_PAR);

	fun->empty_parameter_declaration = 0;
	if (check_and_pop_gently(&tok, TOK_VOID_KW)) {
		check_and_pop(&tok, TOK_CLOSE_PAR);
	} else {
		int is_first_parameter = 1;
		while (tok->type != TOK_CLOSE_PAR) {
			if (!is_first_parameter)
				check_and_pop(&tok, TOK_COMMA);
			check_and_pop(&tok, TOK_INT_KW);
			check_and_pop(&tok, TOK_IDENTIFIER);
			struct ast_var_decl *decl = xcalloc(1, sizeof(*decl));
			decl->name = xstrdup((char *)tok[-1].value);
			decl->tok = &tok[-1];
			ARRAY_APPEND(&fun->parameters, decl);
			is_first_parameter = 0;
		}
		check_and_pop(&tok, TOK_CLOSE_PAR);
		if (is_first_parameter)
			fun->empty_parameter_declaration = 1;
	}

	if (check_and_pop_gently(&tok, TOK_SEMICOLON))
		fun->body = NULL;
	else
		fun->body = parse_statement_block(&tok);

	*tok_ptr = tok;
	return fun;
}

static struct ast_var_decl_list *maybe_parse_global_var_list(struct token **tok_ptr)
{
	struct ast_var_decl_list *decl_list = xcalloc(1, sizeof(*decl_list));
	struct token *tok = *tok_ptr;
	/*
	 * Tells at which point we consider the token sequence a variable
	 * declaration and, thus, we no longer bail if something odd is found.
	 */
	int can_bail = 1;

	if (!check_and_pop_gently(&tok, TOK_INT_KW))
		goto bail;

	while (1) {
		if (can_bail) {
			if (!check_and_pop_gently(&tok, TOK_IDENTIFIER))
				goto bail;
		} else {
			check_and_pop(&tok, TOK_IDENTIFIER);
		}
		struct ast_var_decl *decl = xcalloc(1, sizeof(*decl));
		decl->name = xstrdup((char *)tok[-1].value);
		decl->tok = &tok[-1];
		if (check_and_pop_gently(&tok, TOK_ASSIGNMENT)) {
			can_bail = 0;
			struct token *assign_tok = &tok[-1];
			decl->value = parse_exp_no_comma(&tok);
			if (decl->value->type != AST_EXP_CONSTANT_INT) {
				/*
				 * NEEDSWORK: we should also allow expressions that can
				 * evaluate to a constant int at compile time. For
				 * example: "2 + 2", and "~3".
				 */
				die("static initialization requires a constant value\n%s",
				    show_token_on_source_line(assign_tok));
			}
		}
		ARRAY_APPEND(decl_list, decl);
		if (check_and_pop_gently(&tok, TOK_COMMA))
			can_bail = 0;
		else
			break;
	}

	if (can_bail) {
		if (!check_and_pop_gently(&tok, TOK_SEMICOLON))
			goto bail;
	} else {
		check_and_pop(&tok, TOK_SEMICOLON);
	}

	*tok_ptr = tok;
	return decl_list;

bail:
	free_ast_var_decl_list(decl_list);
	return NULL;
}

struct ast_program *parse_program(struct token *toks)
{
	struct ast_program *prog = xmalloc(sizeof(*prog));
	ARRAY_INIT(&prog->items);

	while (!end_token(toks)) {
		struct ast_toplevel_item *item = xmalloc(sizeof(*item));
		struct ast_var_decl_list *var_list = maybe_parse_global_var_list(&toks);
		if (var_list) {
			item->type = TOPLEVEL_VAR_DECL;
			item->u.var_list = var_list;
		} else {
			item->type = TOPLEVEL_FUNC_DECL;
			item->u.func = parse_func_decl(&toks);
		}
		ARRAY_APPEND(&prog->items, item);
	}

	return prog;
}

/*******************************************************************************
 *			     Memory Freeing
*******************************************************************************/


static void free_ast_expression(struct ast_expression *exp)
{
	switch (exp->type) {
	case AST_EXP_BINARY_OP:
		free_ast_expression(exp->u.bin_op.lexp);
		free_ast_expression(exp->u.bin_op.rexp);
		break;
	case AST_EXP_TERNARY:
		free_ast_expression(exp->u.ternary.condition);
		free_ast_expression(exp->u.ternary.if_exp);
		free_ast_expression(exp->u.ternary.else_exp);
		break;
	case AST_EXP_UNARY_OP:
		free_ast_expression(exp->u.un_op.exp);
		break;
	case AST_EXP_CONSTANT_INT:
		break;
	case AST_EXP_VAR:
		free((char *)exp->u.var.name);
		break;
	case AST_EXP_FUNC_CALL:
		free((char *)exp->u.call.name);
		for (size_t i = 0; i < exp->u.call.args.nr; i++)
			free_ast_expression(exp->u.call.args.arr[i]);
		FREE_ARRAY(&exp->u.call.args);
		break;
	default:
		die("BUG: unknown ast expression type: %d", exp->type);
	}
	free(exp);
}

static void free_ast_var_decl(struct ast_var_decl *decl)
{
	free((char *)decl->name);
	if (decl->value)
		free_ast_expression(decl->value);
	free(decl);
}

static void free_ast_var_decl_list(struct ast_var_decl_list *decl_list)
{
	for (size_t i = 0; i < decl_list->nr; i++) {
		free_ast_var_decl(decl_list->arr[i]);
	}
	FREE_ARRAY(decl_list);
	free(decl_list);
}

static void free_ast_opt_expression(struct ast_opt_expression opt_exp)
{
	if (opt_exp.exp)
		free_ast_expression(opt_exp.exp);
}

static void free_ast_statement(struct ast_statement *st)
{
	switch (st->type) {
	case AST_ST_RETURN:
		free_ast_opt_expression(st->u._return.opt_exp);
		break;
	case AST_ST_VAR_DECL:
		free_ast_var_decl_list(st->u.decl_list);
		break;
	case AST_ST_EXPRESSION:
		free_ast_opt_expression(st->u.opt_exp);
		break;
	case AST_ST_IF_ELSE:
		free_ast_expression(st->u.if_else.condition);
		free_ast_statement(st->u.if_else.if_st);
		if (st->u.if_else.else_st)
			free_ast_statement(st->u.if_else.else_st);
		break;
	case AST_ST_BLOCK:
		for (size_t i = 0; i < st->u.block.nr; i++)
			free_ast_statement(st->u.block.items[i]);
		free(st->u.block.items);
		break;
	case AST_ST_FOR:
		free_ast_opt_expression(st->u._for.prologue);
		free_ast_expression(st->u._for.condition);
		free_ast_opt_expression(st->u._for.epilogue);
		free_ast_statement(st->u._for.body);
		break;
	case AST_ST_FOR_DECL:
		free_ast_var_decl_list(st->u.for_decl.decl_list);
		free_ast_expression(st->u.for_decl.condition);
		free_ast_opt_expression(st->u.for_decl.epilogue);
		free_ast_statement(st->u.for_decl.body);
		break;
	case AST_ST_WHILE:
		free_ast_expression(st->u._while.condition);
		free_ast_statement(st->u._while.body);
		break;
	case AST_ST_DO:
		free_ast_statement(st->u._do.body);
		free_ast_expression(st->u._do.condition);
		break;
	case AST_ST_LABELED_STATEMENT:
		free_ast_statement(st->u.labeled_st.st);
		break;
	case AST_ST_BREAK:
	case AST_ST_CONTINUE:
	case AST_ST_GOTO:
		break;
	default:
		die("BUG: unknown ast statement type: %d", st->type);
	}
	free(st);
}

static void free_ast_func_decl(struct ast_func_decl *fun)
{
	if (fun->body)
		free_ast_statement(fun->body);
	free((char *)fun->name);
	for (size_t i = 0; i < fun->parameters.nr; i++)
		free_ast_var_decl(fun->parameters.arr[i]);
	FREE_ARRAY(&fun->parameters);
	free(fun);
}

static void free_ast_toplevel_item(struct ast_toplevel_item *item)
{
	switch (item->type) {
	case TOPLEVEL_FUNC_DECL:
		free_ast_func_decl(item->u.func);
		break;
	case TOPLEVEL_VAR_DECL:
		free_ast_var_decl_list(item->u.var_list);
		break;
	default:
		BUG("unknown toplevel item '%s'", item->type);
	}
}

static void free_ast_program(struct ast_program *prog)
{
	for (size_t i = 0; i < prog->items.nr; i++)
		free_ast_toplevel_item(prog->items.arr[i]);
	FREE_ARRAY(&prog->items);
	free(prog);
}

void free_ast(struct ast_program *prog)
{
	free_ast_program(prog);
}
