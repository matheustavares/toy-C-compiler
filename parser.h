#ifndef _PARSER_H
#define _PARSER_H

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
	} u;
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
	} type;

	union {
		/* TODO: maybe should unify `ret_exp` and `exp`? */
		struct ast_expression *ret_exp;
		struct ast_expression *exp;
		struct ast_var_decl *decl;
		struct {
			struct ast_expression *condition;
			struct ast_statement *if_st, *else_st; /* else is optional */
		} if_else;
	} u;

	struct ast_statement *next;
};

struct ast_func_decl {
	const char *name;
	struct ast_statement *body;
};

struct ast_program {
	struct ast_func_decl *fun;
};

struct ast_program *parse_program(struct token *toks);
void free_ast(struct ast_program *prog);

#endif
