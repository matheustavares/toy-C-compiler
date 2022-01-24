#include "util.h"
#include "lib/array.h"
#include "lexer.h"

#define ALPHA "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define NUM "0123456789"
#define WHITESPACE " \t\n"
#define IDENTIFIER_HEAD (ALPHA "_")
#define IDENTIFIER_TAIL (ALPHA NUM "_")

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
static int skip_one_char(const char *str, const char *list, const char **skipped)
{
	*skipped = str;
	if (**skipped && char_in(**skipped, list))
		(*skipped)++;
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
	case TOK_COLON: return ":";
	case TOK_QUESTION_MARK: return "?";

	case TOK_INT_KW: return "<int> keyword";
	case TOK_VOID_KW: return "<void> keyword";
	case TOK_RETURN_KW: return "<return> keyword";
	case TOK_IF_KW: return "<if> keyword";
	case TOK_ELSE_KW: return "<else> keyword";
	case TOK_FOR_KW: return "<for> keyword";
	case TOK_WHILE_KW: return "<while> keyword";
	case TOK_DO_KW: return "<do> keyword";
	case TOK_BREAK_KW: return "<break> keyword";
	case TOK_CONTINUE_KW: return "<continue> keyword";
	case TOK_GOTO_KW: return "<goto> keyword";

	case TOK_IDENTIFIER: return "<identifier>";
	case TOK_INTEGER: return "<integer>";

	case TOK_MINUS: return "-";
	case TOK_TILDE: return "~";
	case TOK_PLUS: return "+";
	case TOK_STAR: return "*";
	case TOK_F_SLASH: return "/";
	case TOK_MODULO: return "%";
	case TOK_BITWISE_AND: return "&";
	case TOK_BITWISE_OR: return "|";
	case TOK_BITWISE_XOR: return "^";
	case TOK_BITWISE_LEFT_SHIFT: return "<<";
	case TOK_BITWISE_RIGHT_SHIFT: return ">>";
	case TOK_ASSIGNMENT: return "=";

	case TOK_LOGIC_NOT: return "!";
	case TOK_LOGIC_AND: return "&&";
	case TOK_LOGIC_OR: return "||";
	case TOK_EQUAL: return "==";
	case TOK_NOT_EQUAL: return "!=";
	case TOK_LT: return "<";
	case TOK_LE: return "<=";
	case TOK_GT: return ">";
	case TOK_GE: return ">=";

	case TOK_PLUS_ASSIGNMENT: return "+=";
	case TOK_MINUS_ASSIGNMENT: return "-=";
	case TOK_SLASH_ASSIGNMENT: return "/=";
	case TOK_STAR_ASSIGNMENT: return "*=";
	case TOK_MODULO_ASSIGNMENT: return "%=";
	case TOK_BITWISE_AND_ASSIGNMENT: return "&=";
	case TOK_BITWISE_OR_ASSIGNMENT: return "|=";
	case TOK_BITWISE_XOR_ASSIGNMENT: return "^=";
	case TOK_BITWISE_LEFT_SHIFT_ASSIGNMENT: return "<<=";
	case TOK_BITWISE_RIGHT_SHIFT_ASSIGNMENT: return ">>=";

	case TOK_PLUS_PLUS: return "++";
	case TOK_MINUS_MINUS: return "--";

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

struct lex_ctx {
	struct token *tokens;
	size_t alloc, nr;

	const char *buf, *line_start;
	size_t line_no, col_no;
};

#define LEX_CTX_INIT(buffer) \
	{ .line_no = 1, .buf = (buffer), .line_start = (buffer) }

static void add_token_with_value(struct lex_ctx *ctx, enum token_type type,
				 void *value)
{
	ALLOC_GROW(ctx->tokens, ctx->nr + 1, ctx->alloc);
	struct token *tok = &ctx->tokens[ctx->nr++];
	tok->type = type;
	tok->value = value;
	 /* TODO: it's wasteful to dup the line for evert token in it. */
	tok->line = tab2sp(getline_dup(ctx->line_start), 1);
	tok->line_no = ctx->line_no;
	tok->col_no = ctx->col_no;
}

#define add_token(ctx, type) add_token_with_value(ctx, type, NULL)

static int consume_str(struct lex_ctx *ctx, const char *needle, enum token_type type)
{
	const char *aux;
	if (skip_prefix(ctx->buf, needle, &aux)) {
		add_token(ctx, type);
		ctx->col_no += aux - ctx->buf;
		ctx->buf = aux;
		return 1;
	}
	return 0;
}

static int consume_char(struct lex_ctx *ctx, char needle, enum token_type type)
{
	if (*ctx->buf == needle) {
		add_token(ctx, type);
		(ctx->col_no)++;
		(ctx->buf)++;
		return 1;
	}
	return 0;
}

static int consume_keyword(struct lex_ctx *ctx, char *needle, enum token_type type)
{
	const char *aux;
	if (skip_prefix(ctx->buf, needle, &aux) && !char_in(*aux, IDENTIFIER_TAIL)) {
		add_token(ctx, type);
		ctx->col_no += aux - ctx->buf;
		ctx->buf = aux;
		return 1;
	}
	return 0;
}

static int consume_newline(struct lex_ctx *ctx)
{
	if (*ctx->buf == '\n') {
		ctx->line_start = ++(ctx->buf);
		ctx->line_no++;
		ctx->col_no = 0;
		return 1;
	}
	return 0;
}

static int consume_whitespaces(struct lex_ctx *ctx)
{
	int ret = 0;
	while (char_in(*ctx->buf, WHITESPACE)) {
		ret = 1;
		if (consume_newline(ctx)) {
			;
		} else {
			ctx->col_no++;
			ctx->buf++;
		}
	}
	return ret;
}

static int consume_comments(struct lex_ctx *ctx)
{
	/* Single-line comments. */
	if (*ctx->buf == '/' && *(ctx->buf + 1) == '/') {
		ctx->buf += 2;
		ctx->col_no += 2;
		while (*ctx->buf && *ctx->buf != '\n') {
			ctx->buf++;
			ctx->col_no++;
		}
		return 1;
	}

	/* Multi-line comments. */
	if (*ctx->buf == '/' && *(ctx->buf + 1) == '*') {
		const char *comment_line_start = ctx->line_start;
		size_t comment_line_no = ctx->line_no,
		       comment_col_no = ctx->col_no;
		ctx->buf += 2;
		ctx->col_no += 2;
		while (1) {
			if (*ctx->buf == '*' && *(ctx->buf + 1) == '/') {
				ctx->buf += 2;
				ctx->col_no += 2;
				break;
			} else if (consume_newline(ctx)) {
				continue;
			} else if (!*ctx->buf) {
				die("lexer error: runaway comment block.\n%s",
				    show_on_source_line(
					tab2sp(getline_dup(comment_line_start), 1),
					comment_line_no, comment_col_no));
			} else {
				ctx->buf++;
				ctx->col_no++;
			}
		}
		return 1;
	}

	return 0;
}

struct token *lex(const char *str)
{
	struct lex_ctx ctx = LEX_CTX_INIT(str);

	while (*ctx.buf) {
		const char *aux;

		if (consume_whitespaces(&ctx))
			;
		else if (consume_comments(&ctx))
			;

		else if (consume_char(&ctx, '{', TOK_OPEN_BRACE))
			;
		else if (consume_char(&ctx, '}', TOK_CLOSE_BRACE))
			;
		else if (consume_char(&ctx, '(', TOK_OPEN_PAR))
			;
		else if (consume_char(&ctx, ')', TOK_CLOSE_PAR))
			;
		else if (consume_char(&ctx, ';', TOK_SEMICOLON))
			;
		else if (consume_char(&ctx, ':', TOK_COLON))
			;
		else if (consume_char(&ctx, '?', TOK_QUESTION_MARK))
			;
		else if (consume_char(&ctx, ',', TOK_COMMA))
			;

		else if (consume_str(&ctx, "+=", TOK_PLUS_ASSIGNMENT))
			;
		else if (consume_str(&ctx, "-=", TOK_MINUS_ASSIGNMENT))
			;
		else if (consume_str(&ctx, "/=", TOK_SLASH_ASSIGNMENT))
			;
		else if (consume_str(&ctx, "*=", TOK_STAR_ASSIGNMENT))
			;
		else if (consume_str(&ctx, "%=", TOK_MODULO_ASSIGNMENT))
			;
		else if (consume_str(&ctx, "&=", TOK_BITWISE_AND_ASSIGNMENT))
			;
		else if (consume_str(&ctx, "|=", TOK_BITWISE_OR_ASSIGNMENT))
			;
		else if (consume_str(&ctx, "^=", TOK_BITWISE_XOR_ASSIGNMENT))
			;
		else if (consume_str(&ctx, "<<=", TOK_BITWISE_LEFT_SHIFT_ASSIGNMENT))
			;
		else if (consume_str(&ctx, ">>=", TOK_BITWISE_RIGHT_SHIFT_ASSIGNMENT))
			;

		else if (consume_str(&ctx, "++", TOK_PLUS_PLUS))
			;
		else if (consume_str(&ctx, "--", TOK_MINUS_MINUS))
			;

		else if (consume_char(&ctx, '-', TOK_MINUS))
			;
		else if (consume_char(&ctx, '~', TOK_TILDE))
			;
		else if (consume_char(&ctx, '+', TOK_PLUS))
			;
		else if (consume_char(&ctx, '*', TOK_STAR))
			;
		else if (consume_char(&ctx, '/', TOK_F_SLASH))
			;
		else if (consume_char(&ctx, '%', TOK_MODULO))
			;
		else if (consume_char(&ctx, '^', TOK_BITWISE_XOR))
			;

		else if (consume_str(&ctx, "&&", TOK_LOGIC_AND))
			;
		else if (consume_str(&ctx, "||", TOK_LOGIC_OR))
			;
		else if (consume_str(&ctx, "==", TOK_EQUAL))
			;
		else if (consume_str(&ctx, "!=", TOK_NOT_EQUAL))
			;
		else if (consume_str(&ctx, "<=", TOK_LE))
			;
		else if (consume_str(&ctx, ">=", TOK_GE))
			;

		else if (consume_str(&ctx, "<<", TOK_BITWISE_LEFT_SHIFT))
			;
		else if (consume_str(&ctx, ">>", TOK_BITWISE_RIGHT_SHIFT))
			;

		/* These must come after ">*", "<*", and "*=*" tokens. */
		else if (consume_char(&ctx, '>', TOK_GT))
			;
		else if (consume_char(&ctx, '<', TOK_LT))
			;
		else if (consume_char(&ctx, '!', TOK_LOGIC_NOT))
			;
		else if (consume_char(&ctx, '=', TOK_ASSIGNMENT))
			;

		/* These must come after "&*" and "|*" tokens. */
		else if (consume_char(&ctx, '&', TOK_BITWISE_AND))
			;
		else if (consume_char(&ctx, '|', TOK_BITWISE_OR))
			;

		else if (consume_keyword(&ctx, "int", TOK_INT_KW))
			;
		else if (consume_keyword(&ctx, "void", TOK_VOID_KW))
			;
		else if (consume_keyword(&ctx, "return", TOK_RETURN_KW))
			;
		else if (consume_keyword(&ctx, "if", TOK_IF_KW))
			;
		else if (consume_keyword(&ctx, "else", TOK_ELSE_KW))
			;
		else if (consume_keyword(&ctx, "for", TOK_FOR_KW))
			;
		else if (consume_keyword(&ctx, "while", TOK_WHILE_KW))
			;
		else if (consume_keyword(&ctx, "do", TOK_DO_KW))
			;
		else if (consume_keyword(&ctx, "break", TOK_BREAK_KW))
			;
		else if (consume_keyword(&ctx, "continue", TOK_CONTINUE_KW))
			;
		else if (consume_keyword(&ctx, "goto", TOK_GOTO_KW))
			;

		else if (skip_one_char(ctx.buf, IDENTIFIER_HEAD, &aux) &&
			   skip_chars(ctx.buf, IDENTIFIER_TAIL, &aux) &&
			   !char_in(*aux, IDENTIFIER_TAIL)) {
			add_token_with_value(&ctx, TOK_IDENTIFIER, xstrndup(ctx.buf, aux - ctx.buf));
			ctx.col_no += aux - ctx.buf;
			ctx.buf = aux;
		}

		else if (skip_chars(ctx.buf, NUM, &aux) && !char_in(*aux, IDENTIFIER_TAIL)) {
			int *val = xmalloc(sizeof(*val));
			*val = strtol(ctx.buf, NULL, 10);
			add_token_with_value(&ctx, TOK_INTEGER, val);
			ctx.col_no += aux - ctx.buf;
			ctx.buf = aux;
		}

		else {
			size_t i = 0;
			while (ctx.buf[i] && !char_in(ctx.buf[i], WHITESPACE))
				i++;
			die("lex error: unknown token '%s'\n%s", xstrndup(ctx.buf, i),
			    show_on_source_line(tab2sp(getline_dup(ctx.line_start), 1),
						ctx.line_no, ctx.col_no));
		}
	}
	add_token(&ctx, TOK_NONE); /* sentinel */
	REALLOC_ARRAY(ctx.tokens, ctx.nr); /* trim excess. */
	return ctx.tokens;
}

void free_tokens(struct token *toks)
{
	for (struct token *tok = toks; !end_token(tok); tok++)
		free_token(tok);
	free(toks);
}
