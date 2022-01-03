#ifndef _LEXER_H
#define _LEXER_H

enum token_type {
	TOK_NONE = 0,

	TOK_OPEN_BRACE,
	TOK_CLOSE_BRACE,
	TOK_OPEN_PAR,
	TOK_CLOSE_PAR,
	TOK_SEMICOLON,
	TOK_COLON,
	TOK_QUESTION_MARK,

	/* keywords */
	TOK_INT_KW,
	TOK_RETURN_KW,
	TOK_IF_KW,
	TOK_ELSE_KW,
	TOK_FOR_KW,
	TOK_WHILE_KW,
	TOK_DO_KW,
	TOK_BREAK_KW,
	TOK_CONTINUE_KW,
	TOK_GOTO_KW,

	TOK_IDENTIFIER,
	TOK_INTEGER,

	/* operators */
	TOK_MINUS,
	TOK_TILDE,
	TOK_PLUS,
	TOK_STAR,
	TOK_F_SLASH,
	TOK_MODULO,
	TOK_BITWISE_AND,
	TOK_BITWISE_OR,
	TOK_BITWISE_XOR,
	TOK_BITWISE_LEFT_SHIFT,
	TOK_BITWISE_RIGHT_SHIFT,
	TOK_ASSIGNMENT,

	/* logical operators */
	TOK_LOGIC_NOT,
	TOK_LOGIC_AND,
	TOK_LOGIC_OR,
	TOK_EQUAL,
	TOK_NOT_EQUAL,
	TOK_LT,
	TOK_LE,
	TOK_GT,
	TOK_GE,

	/* compound assignment operators */
	TOK_PLUS_ASSIGNMENT,
	TOK_MINUS_ASSIGNMENT,
	TOK_SLASH_ASSIGNMENT,
	TOK_STAR_ASSIGNMENT,
	TOK_MODULO_ASSIGNMENT,
	TOK_BITWISE_AND_ASSIGNMENT,
	TOK_BITWISE_OR_ASSIGNMENT,
	TOK_BITWISE_XOR_ASSIGNMENT,
	TOK_BITWISE_LEFT_SHIFT_ASSIGNMENT,
	TOK_BITWISE_RIGHT_SHIFT_ASSIGNMENT,

	/* postfix and prefix incrementors/decrementors  */
	TOK_PLUS_PLUS,
	TOK_MINUS_MINUS,
};

struct token {
	enum token_type type;
	void *value;

	/* Token to source file mapping. */
	const char *line;
	size_t line_no, col_no;
};

struct token *lex(const char *str);
void print_token(struct token *t);

/*
 * Note: tt2str returns a static non-thread-safe buffer; tok2str returns a
 * malloc'ed buffer, which must be free'd.
 */
const char *tt2str(enum token_type tt);
char *tok2str(struct token *t);

char *show_token_on_source_line(struct token *tok);

void free_tokens(struct token *toks);
void free_token(struct token *t);

#define end_token(tok) ((tok)->type == TOK_NONE)

#endif
