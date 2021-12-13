#include "util.h"
#include "lexer.h"
#include "parser.h"

/*
Rules:
(from https://norasandler.com/2017/11/29/Write-a-Compiler.html)
 
program = Program(function_declaration)
function_declaration = Function(string, statement) //string is the function name
statement = Return(exp)
exp = Constant(int) 

<program> ::= <function_declaration>
<function_declaration> ::= "int" <id> "(" ")" "{" <statement> "}"
<statement> ::= "return" <exp> ";"
<exp> ::= <int>
*/

/*******************************************************************************
 *				Parsing
*******************************************************************************/

#define expect_type(tok, etype) \
	do { \
		if ((tok)->type != (etype)) \
			die("parser: expecting '%s' got '%s'\n%s", \
			    tt2str(etype), tok2str(tok), \
			    show_token_on_source_line(tok)); \
		(tok)++; \
	} while(0)

static struct ast_expression *parse_exp(struct token **tok_ptr)
{
	struct ast_expression *exp = xmalloc(sizeof(*exp));
	struct token *tok = *tok_ptr;

	expect_type(tok, TOK_INTEGER);
	exp->type = AST_EXP_CONSTANT_INT;
	exp->u.ival = *((int *)tok[-1].value);

	*tok_ptr = tok;
	return exp;
}

static struct ast_statement *parse_statement(struct token **tok_ptr)
{
	struct ast_statement *st = xmalloc(sizeof(*st));
	struct token *tok = *tok_ptr;

	expect_type(tok, TOK_RETURN_KW);
	st->type = AST_ST_RETURN;
	st->u.ret_exp = parse_exp(&tok);
	expect_type(tok, TOK_SEMICOLON);

	*tok_ptr = tok;
	return st;
}

static struct ast_func_decl *parse_func_decl(struct token **tok_ptr)
{
	struct ast_func_decl *fun = xmalloc(sizeof(*fun));
	struct token *tok = *tok_ptr;

	expect_type(tok, TOK_INT_KW);
	expect_type(tok, TOK_IDENTIFIER);
	fun->name = xstrdup((char *)tok[-1].value);
	expect_type(tok, TOK_OPEN_PAR);
	expect_type(tok, TOK_CLOSE_PAR);
	expect_type(tok, TOK_OPEN_BRACE);
	fun->body = parse_statement(&tok);
	expect_type(tok, TOK_CLOSE_BRACE);

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
	case AST_EXP_CONSTANT_INT:
		free(exp);
		break;
	default:
		die("BUG: unknown ast expression type: %d", exp->type);
	}
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

static void print_ast_expression(struct ast_expression *exp)
{
	switch (exp->type) {
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
