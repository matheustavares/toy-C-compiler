#ifndef _PARSER_H
#define _PARSER_H

#include "lexer.h"

/*
TODO: can we possibly simplify/unify the functions using some generic node
structure? Perhaps:

enum ast_node_type {
	AST_NONE = 0,
	AST_PROGRAM,
	AST_FUNC_DECL,
	AST_STATEMENT,
	AST_EXPRESSION,
};

struct ast_node {
	enum ast_node_type type;
	union {
		struct ast_expression *exp;
		struct ast_statement *st;
		struct ast_func_decl *fun;
		struct ast_program *prog;
	} u;
};
*/

struct ast_expression {
	enum {
		AST_EXP_CONSTANT_INT,
		AST_EXP_UNARY_OP,
	} type;

	union {
		int ival;
		struct {
			enum un_op_type {
				EXP_OP_NEGATION,
				EXP_OP_BIT_COMPLEMENT,
				EXP_OP_LOGIC_NEGATION,
			} type;
			struct ast_expression *exp;
		} un_op;
	} u;
};

struct ast_statement {
	enum {
		AST_ST_RETURN,
	} type;

	union {
		struct ast_expression *ret_exp;
	} u;
};

struct ast_func_decl {
	char *name;
	struct ast_statement *body;
};

struct ast_program {
	struct ast_func_decl *fun;
};

struct ast_program *parse_program(struct token *toks);
void print_ast_in_dot(struct ast_program *prog);

void free_ast(struct ast_program *prog);

#endif
