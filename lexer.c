#include "util.h"
#include "lib/array.h"
#include "lexer.h"

#define ALPHA "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define NUM "012345689"
#define WHITESPACE " \t\n"
#define ALPHA_NUM (ALPHA NUM)

/*
 * Do not use this function if you want to include '\0' in `list`. As soon as
 * it finds '\0', it terminates the search unsuccessfully.
 */
static int char_in(char c, const char *list)
{
	for (; *list; list++)
		if (c == *list)
			return 1;
	return 0;
}

/* 
 * Stores in `skipped` a pointer to the first char at `str` that is not in
 * `list`. Returns 1 iff skipped was moved forward in `str`.
 */
static int skip_chars(const char *str, const char *list, const char **skipped)
{
	for (*skipped = str; **skipped && char_in(**skipped, list); (*skipped)++)
		;
	return *skipped != str;
}

static char *show_on_source_line(const char *line, size_t line_no, size_t col_no)
{
	/*
	 * TODO: could probably use Git's strbuf API for this.
	 * And a static buffer, so that the caller doesn't have to free it.
	 */
	char *prefix = xmkstr("On line %zu: ", line_no);
	char *ret = xmkstr("%s%s\n%*c^", prefix, line,
			   col_no + strlen(prefix), ' ');
	free(prefix);
	return ret;
}

char *show_token_on_source_line(struct token *tok)
{
	return show_on_source_line(tok->line, tok->line_no, tok->col_no);
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
	free((char *)t->line);
}


struct token *lex(const char *str)
{
	struct token *tokens = NULL;
	size_t alloc = 0, nr = 0;
	const char *line_start = str;
	size_t line_no = 0, col_no = 0;

#define add_token_with_value(t, v) \
	do { \
		ALLOC_GROW(tokens, nr + 1, alloc); \
		tokens[nr].type = t; \
		tokens[nr].value = v; \
		 /* TODO: it's wasteful to dup the line for evert token in it. */ \
		tokens[nr].line = getline_dup(line_start); \
		tokens[nr].line_no = line_no; \
		tokens[nr].col_no = col_no; \
		nr++; \
	} while (0)

#define add_token(t) add_token_with_value(t, NULL)

	for (; *str; str++, col_no++) {
		while (char_in(*str, " \t")) {
			str++; col_no++;
		}
		if (!*str)
			break;
		if (*str == '\n') {
			line_start = str + 1;
			line_no++;
			col_no = -1; /* Adjust for the loop's 'col_no++' */
			continue;
		}

		const char *aux;

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

		} else if (skip_prefix(str, "int", &aux) && !char_in(*aux, ALPHA_NUM)) {
			add_token(TOK_INT_KW);
			col_no += aux - 1 - str;
			str = aux - 1;
		} else if (skip_prefix(str, "return", &aux) && !char_in(*aux, ALPHA_NUM)) {
			add_token(TOK_RETURN_KW);
			col_no += aux - 1 - str;
			str = aux - 1;
		/* TODO: allow '_' and '[a-Z]+[0-9]' identifiers. */
		} else if (skip_chars(str, ALPHA, &aux) && !char_in(*aux, NUM)) {
			add_token_with_value(TOK_IDENTIFIER, xstrndup(str, aux - str));
			col_no += aux - 1 - str;
			str = aux - 1;
		} else if (skip_chars(str, NUM, &aux) && !char_in(*aux, ALPHA)) {
			int *val = xmalloc(sizeof(*val));
			*val = strtol(str, NULL, 10);
			add_token_with_value(TOK_INTEGER, val);
			col_no += aux - 1 - str;
			str = aux - 1;
		} else {
			size_t i;
			for (i = 0; str[i] && !char_in(str[i], WHITESPACE); i++)
				;
			die("lex error: unknown token '%s'\n%s", xstrndup(str, i),
			    show_on_source_line(getline_dup(line_start),
						line_no, col_no));
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
