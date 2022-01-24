#ifndef _PARSER_H
#define _PARSER_H

#include "lib/array.h"

struct token;

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
		AST_EXP_BINARY_OP,
		AST_EXP_VAR,
		AST_EXP_TERNARY,
		AST_EXP_FUNC_CALL,
	} type;

	union {
		int ival;
		struct {
			enum un_op_type {
				EXP_OP_NEGATION,
				EXP_OP_BIT_COMPLEMENT,
				EXP_OP_LOGIC_NEGATION,
				EXP_OP_PREFIX_INC,
				EXP_OP_SUFFIX_INC,
				EXP_OP_PREFIX_DEC,
				EXP_OP_SUFFIX_DEC,
			} type;
			struct ast_expression *exp;
		} un_op;
		struct {
			enum bin_op_type {
				EXP_OP_ADDITION = 0,
				EXP_OP_SUBTRACTION,
				EXP_OP_DIVISION,
				EXP_OP_MULTIPLICATION,
				EXP_OP_MODULO,

				EXP_OP_LOGIC_AND,
				EXP_OP_LOGIC_OR,
				EXP_OP_EQUAL,
				EXP_OP_NOT_EQUAL,
				EXP_OP_LT,
				EXP_OP_LE,
				EXP_OP_GT,
				EXP_OP_GE,

				EXP_OP_BITWISE_AND,
				EXP_OP_BITWISE_OR,
				EXP_OP_BITWISE_XOR,
				EXP_OP_BITWISE_LEFT_SHIFT,
				EXP_OP_BITWISE_RIGHT_SHIFT,

				/*
				 * Note: although this is listed as a binary
				 * operator (and it indeed inceeds upon two
				 * args), it is different than the other
				 * operators in the sense that its left exp
				 * MUST be of type AST_EXP_VAR. We don't
				 * enforce this here, in the struct definition,
				 * but we don in the parser.
				 */
				EXP_OP_ASSIGNMENT,

				/* Keep at the end. */
				EXP_BIN_OP_NR,
			} type;
			struct ast_expression *lexp, *rexp;
		} bin_op;

		struct var_ref {
			const char *name;
			struct token *tok;
		} var;

		struct {
			struct ast_expression *condition,
					      *if_exp,
					      *else_exp;
		} ternary;

		struct func_call {
			const char *name;
			ARRAY(struct ast_expression *) args;
			struct token *tok;
		} call;
	} u;
};

/* A possibly null expression. */
struct ast_opt_expression {
	struct ast_expression *exp;
};

struct ast_var_decl {
	const char *name;
	struct token *tok;
	struct ast_expression *value; /* optional */
};

struct ast_statement {
	enum {
		AST_ST_RETURN,
		/*
		 * TODO: this should probably not be at ast_statement. See
		 * parser.c:parse_statement_1() for more info.
		 */
		AST_ST_VAR_DECL,
		AST_ST_EXPRESSION,
		AST_ST_IF_ELSE,
		AST_ST_BLOCK,
		AST_ST_FOR,
		AST_ST_FOR_DECL,
		AST_ST_WHILE,
		AST_ST_DO,
		AST_ST_BREAK,
		AST_ST_CONTINUE,
		AST_ST_GOTO,
		AST_ST_LABELED_STATEMENT,
	} type;

	union {
		struct ast_expression *ret_exp;
		struct ast_opt_expression opt_exp;
		struct ast_var_decl *decl;
		struct if_else {
			struct ast_expression *condition;
			struct ast_statement *if_st, *else_st; /* else is optional */
		} if_else;

		struct block {
			struct ast_statement **items;
			size_t alloc, nr;
		} block;

		struct {
			struct ast_opt_expression prologue;
			struct ast_expression *condition; /* must default to true when empty */
			struct ast_opt_expression epilogue;
			struct ast_statement *body;
		} _for;

		struct {
			struct ast_var_decl *decl;
			struct ast_expression *condition; /* must default to true when empty */
			struct ast_opt_expression epilogue;
			struct ast_statement *body;
		} for_decl;

		struct {
			struct ast_expression *condition;
			struct ast_statement *body;
		} _while;

		struct {
			struct ast_statement *body;
			struct ast_expression *condition;
		} _do;

		struct token *continue_tok,
			     *break_tok;

		struct {
			const char *label;
			struct token *label_tok;
		} _goto;

		struct {
			const char *label;
			struct token *label_tok;
			struct ast_statement *st;
		} labeled_st;
	} u;
};

struct ast_func_decl {
	const char *name;
	struct token *tok;
	/* True if func was declared as `func()`. But not `func(void)`! */
	int empty_parameter_declaration:1;
	ARRAY(struct ast_var_decl *) parameters;
	/*
	 * Optional. If present, must be of type AST_ST_BLOCK.
	 * (Enforced by parser.c)
	 */
	struct ast_statement *body;
};

struct ast_program {
	ARRAY(struct ast_func_decl *) funcs;
};

struct ast_program *parse_program(struct token *toks);
void free_ast(struct ast_program *prog);

#endif
