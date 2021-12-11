#ifndef _LEXER_H
#define _LEXER_H

enum token_type {
	TT_NONE = 0,

	TT_OPEN_BRACE,
	TT_CLOSE_BRACE,
	TT_OPEN_PAR,
	TT_CLOSE_PAR,
	TT_SEMICOLON,

	/* keywords */
	TT_INT_KW,
	TT_RETURN_KW,

	TT_IDENTIFIER,
	TT_INTEGER,
};

struct token {
	enum token_type type;
	void *value;
};

struct token *lex(const char *str, size_t *nr);
void print_token(struct token *t);
void free_token(struct token *t);

#endif
