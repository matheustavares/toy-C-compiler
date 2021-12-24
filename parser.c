#include "util.h"
#include "lexer.h"
#include "parser.h"

/*******************************************************************************
 *				Parsing
*******************************************************************************/

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

static struct ast_expression *parse_exp(struct token **tok_ptr);
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
		/*
		 * TODO: considering unary operators as part of the atom only
		 * works now because our unary operators have the highest
		 * precedence among all implemented operators. But if we add
		 * "->", ".", "++", "() (function call)" or others with a
		 * higher precedence, we must take care.
		 */
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

/* 
 * Parse expression using precedence climbing.
 * See: https://eli.thegreenplace.net/2012/08/02/parsing-expressions-by-precedence-climbing.
 */
static struct ast_expression *parse_exp_1(struct token **tok_ptr, int min_prec)
{
	struct token *op_tok, *tok = *tok_ptr;
	struct ast_expression *exp = parse_exp_atom(&tok);

	while (is_bin_op_tok(tok->type)) {
		enum bin_op_type compound_op;
		enum bin_op_type bin_op_type = tt2bin_op_type(tok->type);
		int prec = bin_op_precedence(bin_op_type);

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
			parse_exp_1(&tok, assoc == ASSOC_LEFT ? prec + 1 : prec);

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
	return parse_exp_1(tok_ptr, 1);
}

static struct ast_var_decl *parse_var_decl(struct token **tok_ptr)
{
	struct ast_var_decl *decl = xcalloc(1, sizeof(*decl));
	struct token *tok = *tok_ptr;

	check_and_pop(&tok, TOK_INT_KW);
	check_and_pop(&tok, TOK_IDENTIFIER);
	decl->name = xstrdup((char *)tok[-1].value);
	decl->tok = &tok[-1];
	if (check_and_pop_gently(&tok, TOK_ASSIGNMENT))
		decl->value = parse_exp(&tok);

	*tok_ptr = tok;
	return decl;
}

static struct ast_statement *parse_statement(struct token **tok_ptr)
{
	struct ast_statement *st = xcalloc(1, sizeof(*st));
	struct token *tok = *tok_ptr;

	if (check_and_pop_gently(&tok, TOK_RETURN_KW)) {
		st->type = AST_ST_RETURN;
		st->u.ret_exp = parse_exp(&tok);
	} else if (tok->type == TOK_INT_KW) {
		st->type = AST_ST_VAR_DECL;
		st->u.decl = parse_var_decl(&tok);
	} else {
		/* must be an expression */
		st->type = AST_ST_EXPRESSION;
		st->u.exp = parse_exp(&tok);
	}

	check_and_pop(&tok, TOK_SEMICOLON);

	*tok_ptr = tok;
	return st;
}

static struct ast_func_decl *parse_func_decl(struct token **tok_ptr)
{
	struct ast_func_decl *fun = xcalloc(1, sizeof(*fun));
	struct ast_statement **last_st = &(fun->body);
	struct token *tok = *tok_ptr;

	check_and_pop(&tok, TOK_INT_KW);
	check_and_pop(&tok, TOK_IDENTIFIER);
	fun->name = xstrdup((char *)tok[-1].value);
	check_and_pop(&tok, TOK_OPEN_PAR);
	check_and_pop(&tok, TOK_CLOSE_PAR);
	check_and_pop(&tok, TOK_OPEN_BRACE);
	while (!end_token(tok) && tok->type != TOK_CLOSE_BRACE) {
		*last_st = parse_statement(&tok);
		last_st = &((*last_st)->next);
	}
	check_and_pop(&tok, TOK_CLOSE_BRACE);

	*tok_ptr = tok;
	return fun;
}

struct ast_program *parse_program(struct token *toks)
{
	struct ast_program *prog = xmalloc(sizeof(*prog));
	prog->fun = parse_func_decl(&toks);
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
	case AST_EXP_UNARY_OP:
		free_ast_expression(exp->u.un_op.exp);
		break;
	case AST_EXP_CONSTANT_INT:
		break;
	case AST_EXP_VAR:
		free((char *)exp->u.var.name);
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

static void free_ast_statement(struct ast_statement *st)
{
	switch (st->type) {
	case AST_ST_RETURN:
		free_ast_expression(st->u.ret_exp);
		break;
	case AST_ST_VAR_DECL:
		free_ast_var_decl(st->u.decl);
		break;
	case AST_ST_EXPRESSION:
		free_ast_expression(st->u.exp);
		break;
	default:
		die("BUG: unknown ast statement type: %d", st->type);
	}
	free(st);
}

static void free_ast_func_decl(struct ast_func_decl *fun)
{
	struct ast_statement *st = fun->body;
	while (st) {
		struct ast_statement *next = st->next;
		free_ast_statement(st);
		st = next;
	}
	free((char *)fun->name);
	free(fun);
}

static void free_ast_program(struct ast_program *prog)
{
	free_ast_func_decl(prog->fun);
	free(prog);
}

void free_ast(struct ast_program *prog)
{
	free_ast_program(prog);
}
