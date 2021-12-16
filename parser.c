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
static void check_and_pop_1(struct token **tok_ptr, ...)
{
	va_list args;
	va_start(args, tok_ptr);
	for (enum token_type etype = va_arg(args, enum token_type);
	     etype != TOK_NONE;
	     etype = va_arg(args, enum token_type)) {
		if ((*tok_ptr)->type == etype) {
			va_end(args);
			(*tok_ptr)++;
			return;
		}
	}
	va_end(args);

	va_start(args, tok_ptr);
	die("parser: expecting %s got %s\n%s", \
	    str_join_token_types("or", args), tok2str(*tok_ptr), \
			    show_token_on_source_line(*tok_ptr)); \
	va_end(args);
}

#define check_and_pop(tok_ptr, ...) \
	check_and_pop_1(tok_ptr, __VA_ARGS__, TOK_NONE)

static enum un_op_type tt2un_op_type(enum token_type type)
{
	switch (type) {
	case TOK_MINUS: return EXP_OP_NEGATION;
	case TOK_TILDE: return EXP_OP_BIT_COMPLEMENT;
	case TOK_EXCLAMATION: return EXP_OP_LOGIC_NEGATION;
	default: die("BUG: unknown token type at tt2un_op_type: %d", type);
	}
}

static struct ast_expression *parse_exp(struct token **tok_ptr);
static struct ast_expression *parse_exp_atom(struct token **tok_ptr)
{
	struct ast_expression *exp;
	struct token *tok = *tok_ptr;

	check_and_pop(&tok, TOK_INTEGER, TOK_OPEN_PAR, TOK_MINUS, TOK_TILDE,
		      TOK_EXCLAMATION, TOK_PLUS);

	if (tok[-1].type == TOK_INTEGER) {
		exp = xmalloc(sizeof(*exp));
		exp->type = AST_EXP_CONSTANT_INT;
		exp->u.ival = *((int *)tok[-1].value);
	} else if (tok[-1].type == TOK_OPEN_PAR) {
		exp = parse_exp(&tok);
		check_and_pop(&tok, TOK_CLOSE_PAR);
	} else if (tok[-1].type == TOK_PLUS) {
		/* This unary operator does nothing, so we just remove it. */
		exp = parse_exp_atom(&tok);
	} else {
		exp = xmalloc(sizeof(*exp));
		exp->type = AST_EXP_UNARY_OP;
		exp->u.un_op.type = tt2un_op_type(tok[-1].type);
		exp->u.un_op.exp = parse_exp_atom(&tok);
	}

	*tok_ptr = tok;
	return exp;
}

static inline int is_bin_op_tok(enum token_type tt)
{
	return tt == TOK_PLUS || tt == TOK_MINUS || tt == TOK_F_SLASH ||
	       tt == TOK_STAR;
}

static enum un_op_type tt2bin_op_type(enum token_type type)
{
	switch (type) {
	case TOK_MINUS: return EXP_OP_SUBTRACTION;
	case TOK_PLUS: return EXP_OP_ADDITION;
	case TOK_STAR: return EXP_OP_MULTIPLICATION;
	case TOK_F_SLASH: return EXP_OP_DIVISION;
	default: die("BUG: unknown token type at tt2bin_op_type: %d", type);
	}
}

static int bin_op_precedence(enum bin_op_type type)
{
	/*
	 * Source: https://en.cppreference.com/w/c/language/operator_precedence
	 * But we invert the number sequence to have the highest value at the
	 * highest precedence group.
	 */
	switch (type) {
	case EXP_OP_SUBTRACTION: return 12;
	case EXP_OP_ADDITION: return 12;
	case EXP_OP_MULTIPLICATION: return 13;
	case EXP_OP_DIVISION: return 13;
	default: die("BUG: unknown type at bin_op_precedence: %d", type);
	}
}

enum associativity { ASSOC_LEFT, ASSOC_RIGHT };
static enum associativity bin_op_associativity(enum bin_op_type type)
{
	switch (type) {
	case EXP_OP_SUBTRACTION: return ASSOC_LEFT;
	case EXP_OP_ADDITION: return ASSOC_LEFT;
	case EXP_OP_MULTIPLICATION: return ASSOC_LEFT;
	case EXP_OP_DIVISION: return ASSOC_LEFT;
	default: die("BUG: unknown type at bin_op_precedence: %d", type);
	}
}

/* 
 * Parse expression using precedence climbing.
 * See: https://eli.thegreenplace.net/2012/08/02/parsing-expressions-by-precedence-climbing.
 */
static struct ast_expression *parse_exp_1(struct token **tok_ptr, int min_prec)
{
	struct token *tok = *tok_ptr;
	struct ast_expression *exp = parse_exp_atom(&tok);

	while (is_bin_op_tok(tok->type)) {
		enum bin_op_type bin_op_type = tt2bin_op_type(tok->type);
		int prec = bin_op_precedence(bin_op_type);
		if (prec < min_prec)
			break;
		tok++;

		enum associativity assoc = bin_op_associativity(bin_op_type);

		struct ast_expression *lexp = exp;
		struct ast_expression *rexp =
			parse_exp_1(&tok, assoc == ASSOC_LEFT ? prec + 1 : prec);

		exp = xmalloc(sizeof(*exp));
		exp->type = AST_EXP_BINARY_OP;
		exp->u.bin_op.type = bin_op_type;
		exp->u.bin_op.lexp = lexp;
		exp->u.bin_op.rexp = rexp;
	}

	*tok_ptr = tok;
	return exp;
}

static struct ast_expression *parse_exp(struct token **tok_ptr)
{
	return parse_exp_1(tok_ptr, 1);
}

static struct ast_statement *parse_statement(struct token **tok_ptr)
{
	struct ast_statement *st = xmalloc(sizeof(*st));
	struct token *tok = *tok_ptr;

	check_and_pop(&tok, TOK_RETURN_KW);
	st->type = AST_ST_RETURN;
	st->u.ret_exp = parse_exp(&tok);
	check_and_pop(&tok, TOK_SEMICOLON);

	*tok_ptr = tok;
	return st;
}

static struct ast_func_decl *parse_func_decl(struct token **tok_ptr)
{
	struct ast_func_decl *fun = xmalloc(sizeof(*fun));
	struct token *tok = *tok_ptr;

	check_and_pop(&tok, TOK_INT_KW);
	check_and_pop(&tok, TOK_IDENTIFIER);
	fun->name = xstrdup((char *)tok[-1].value);
	check_and_pop(&tok, TOK_OPEN_PAR);
	check_and_pop(&tok, TOK_CLOSE_PAR);
	check_and_pop(&tok, TOK_OPEN_BRACE);
	fun->body = parse_statement(&tok);
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
	default:
		die("BUG: unknown ast expression type: %d", exp->type);
	}
	free(exp);
}

static void free_ast_statement(struct ast_statement *st)
{
	switch (st->type) {
	case AST_ST_RETURN:
		free_ast_expression(st->u.ret_exp);
		free(st);
		break;
	default:
		die("BUG: unknown ast statement type: %d", st->type);
	}
}

static void free_ast_func_decl (struct ast_func_decl *fun)
{
	free_ast_statement(fun->body);
	free(fun->name);
	free(fun);
}

static void free_ast_program(struct ast_program *prog)
{
	free(prog);
}


void free_ast(struct ast_program *prog)
{
	free_ast_program(prog);
}

/*******************************************************************************
 *				Printing
*******************************************************************************/

static const char *un_op_as_str(enum un_op_type type)
{
	switch (type) {
	case EXP_OP_NEGATION: return "-";
	case EXP_OP_BIT_COMPLEMENT: return "~";
	case EXP_OP_LOGIC_NEGATION: return "!";
	default: die("BUG: unknown un_op type %d", type);
	}
}

static const char *bin_op_as_str(enum bin_op_type type)
{
	switch (type) {
	case EXP_OP_ADDITION: return "+";
	case EXP_OP_SUBTRACTION: return "-";
	case EXP_OP_DIVISION: return "/";
	case EXP_OP_MULTIPLICATION: return "*";
	default: die("BUG: unknown bin_op type %d", type);
	}
}

static void print_ast_expression(struct ast_expression *exp)
{
	const char *type_str;
	switch (exp->type) {
	case AST_EXP_BINARY_OP:
		type_str = bin_op_as_str(exp->u.bin_op.type);
		printf("\"Binary Op '%s'\";\n", type_str);
		printf("  \"Binary Op '%s'\" -> ", type_str);
		print_ast_expression(exp->u.bin_op.lexp);
		printf("  \"Binary Op '%s'\" -> ", type_str);
		print_ast_expression(exp->u.bin_op.rexp);
		break;
	case AST_EXP_UNARY_OP:
		type_str = un_op_as_str(exp->u.un_op.type);
		printf("\"Unary Op '%s'\";\n", type_str);
		printf("  \"Unary Op '%s'\" -> ", type_str);
		print_ast_expression(exp->u.un_op.exp);
		break;
	case AST_EXP_CONSTANT_INT:
		printf("\"Constant int '%d'\";\n", exp->u.ival);
		break;
	default:
		die("BUG: unknown ast expression type: %d", exp->type);
	}
}

static void print_ast_statement(struct ast_statement *st)
{
	switch (st->type) {
	case AST_ST_RETURN:
		printf("Return;\n");
		printf("  Return -> ");
		print_ast_expression(st->u.ret_exp);
		break;
	default:
		die("BUG: unknown ast statement type: %d", st->type);
	}
}

static void print_ast_func_decl (struct ast_func_decl *fun)
{
	printf("\"Function: %s\";\n", fun->name);
	printf("  \"Function: %s\" -> ", fun->name);
	print_ast_statement(fun->body);
}

static void print_ast_program(struct ast_program *prog)
{
	printf("  Program -> ");
	print_ast_func_decl(prog->fun);
}

void print_ast_in_dot(struct ast_program *prog)
{
	printf("strict digraph {\n");
	print_ast_program(prog);
	printf("}\n");
}
