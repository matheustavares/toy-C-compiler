#ifndef _SYMTABLE_H
#define _SYMTABLE_H

#include "lib/strmap.h"
#include "parser.h"

struct token;

struct sym_data {
	enum {
		SYM_LOCAL_VAR,
		/* SYM_GLOBAL_VAR, */
		/* SYM_FUNC, */
	} type;
	union {
		size_t stack_index; /* SYM_LOCAL_VAR */
		/* char *label; */ /* SYM_GLOBAL_VAR */
		/* struct ... */ /* SYM_FUNC */
	} u;
	struct token *tok;
	unsigned int scope;
};

struct symtable {
	/* This maps 'char *'s to size_t's, which are indexes at data[]. */
	struct strmap syms;
	struct sym_data *data;
	size_t nr, alloc;
};

void symtable_init(struct symtable *tab);
void symtable_cpy(struct symtable *dst, struct symtable *src);
void symtable_destroy(struct symtable *tab);
struct sym_data *symtable_find(struct symtable *tab, const char *symname);
int symtable_has(struct symtable *tab, const char *symname);

void symtable_put_lvar(struct symtable *tab, struct ast_var_decl *decl,
		       size_t stack_index, unsigned int scope);
size_t symtable_var_ref(struct symtable *tab, struct var_ref *v);

#endif
