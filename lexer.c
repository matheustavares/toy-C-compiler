#include "util.h"
#include "lib/array.h"
#include "lexer.h"

#define ALPHA "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define NUM "012345689"
#define WHITESPACE " \t\n"
#define ALPHA_NUM (ALPHA NUM)

static int char_in(char c, const char *list)
{
	for (; *list; list++)
		if (c == *list)
			return 1;
	return 0;
}

/* 
 * Advances `str` to the first char that is not in `list`.
 * Returns 1 iff at least one position was advanced.
 */
static int skip_chars(const char **str_p, const char *list)
{
	int advanced = 0;
	const char *str = *str_p;
	for (; *str && char_in(*str, list); str++)
		advanced = 1;
	if (advanced)
		*str_p = str;
	return advanced;
}

const char *tt2str(enum token_type tt)
{
	switch(tt) {
	case TOK_NONE: return "[none]";

	case TOK_OPEN_BRACE: return "{";
	case TOK_CLOSE_BRACE: return "}";
	case TOK_OPEN_PAR: return "(";
	case TOK_CLOSE_PAR: return ")";
	case TOK_SEMICOLON: return ";";

	case TOK_INT_KW: return "<int> keyword";
	case TOK_RETURN_KW: return "<return> keyword";

	case TOK_IDENTIFIER: return "<identifier>";
	case TOK_INTEGER: return "<integer>";
	default:
		die("Unknown token type: %d\n", tt);
	}
}

char *tok2str(struct token *t)
{
	const char *type_str = tt2str(t->type);
	switch(t->type) {
	case TOK_IDENTIFIER:
		return xmkstr("%s '%s'", type_str, (char *)t->value);
	case TOK_INTEGER:
		return xmkstr("%s '%d'", type_str, *(int *)(t->value));
	default:
		return xstrdup(type_str);
	}
}

void print_token(struct token *t)
{
	char *tok_str = tok2str(t);
	printf("%s\n", tok_str);
	free(tok_str);
}

void free_token(struct token *t)
{
	t->type = TOK_NONE;
	FREE_AND_NULL(t->value);
}


struct token *lex(const char *str)
{
	struct token *tokens = NULL;
	size_t alloc = 0, nr = 0;

#define add_token_with_value(t, v) \
	do { \
		ALLOC_GROW(tokens, nr + 1, alloc); \
		tokens[nr].type = t; \
		tokens[nr].value = v; \
		nr++; \
	} while (0)

#define add_token(t) add_token_with_value(t, NULL)

	for (; *str; str++) {
		while (char_in(*str, WHITESPACE))
			str++;
		if (!*str)
			break;

		const char *start = str;

		if (*str == '{') {
			add_token(TOK_OPEN_BRACE);
		} else if (*str == '}') {
			add_token(TOK_CLOSE_BRACE);
		} else if (*str == '(') {
			add_token(TOK_OPEN_PAR);
		} else if (*str == ')') {
			add_token(TOK_CLOSE_PAR);
		} else if (*str == ';') {
			add_token(TOK_SEMICOLON);

		} else if (skip_prefix(str, "int", &str) && !char_in(*str, ALPHA_NUM)) {
			add_token(TOK_INT_KW);
			str--;
		} else if (skip_prefix(str, "return", &str) && !char_in(*str, ALPHA_NUM)) {
			add_token(TOK_RETURN_KW);
			str--;
		/* TODO: allow '_' and '[a-Z]+[0-9]' identifiers. */
		} else if (skip_chars(&str, ALPHA) && !char_in(*str, NUM)) {
			add_token_with_value(TOK_IDENTIFIER, strndup(start, str - start));
			str--;
		} else if (skip_chars(&str, NUM) && !char_in(*str, ALPHA)) {
			int *val = xmalloc(sizeof(*val));
			*val = strtol(start, NULL, 10);
			add_token_with_value(TOK_INTEGER, val);
			str--;
		} else {
			const char *end = str;
			while (*end && !char_in(*end, WHITESPACE))
				end++;
			*(char *)end = '\0';
			die("lex error: unknown token '%s'", str);

		}
	}
	add_token(TOK_NONE); /* sentinel */
	REALLOC_ARRAY(tokens, nr); /* trim excess. */
	return tokens;
}

void free_tokens(struct token *toks)
{
	for (struct token *tok = toks; !end_token(tok); tok++)
		free_token(tok);
	free(toks);
}
