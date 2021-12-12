#ifndef _LEXER_H
#define _LEXER_H

enum token_type {
	TOK_NONE = 0,

	TOK_OPEN_BRACE,
	TOK_CLOSE_BRACE,
	TOK_OPEN_PAR,
	TOK_CLOSE_PAR,
	TOK_SEMICOLON,

	/* keywords */
	TOK_INT_KW,
	TOK_RETURN_KW,

	TOK_IDENTIFIER,
	TOK_INTEGER,
};

struct token {
	enum token_type type;
	void *value;
};

struct token *lex(const char *str);
void print_token(struct token *t);

/*
 * Note: tt2str returns a static non-thread-safe buffer; tok2str returns a
 * malloc'ed buffer, which must be free'd.
 */
const char *tt2str(enum token_type tt);
char *tok2str(struct token *t);

void free_tokens(struct token *toks);
void free_token(struct token *t);

#define end_token(tok) ((tok)->type == TOK_NONE)

#endif
