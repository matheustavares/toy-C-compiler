#include "util.h"
#include "lib/strmap.h"
#include "lib/array.h"
#include "symtable.h"
#include "parser.h"
#include "lexer.h"

static void plain_cpy(void **ptr_dst, void **ptr_src)
{
	*ptr_dst = *ptr_src;
}

static void sym_data_cpy(struct sym_data *dst, struct sym_data *src)
{
	/* We can do a shallow copy for now. */
	*dst = *src;
}

void symtable_init(struct symtable *tab)
{
	memset(tab, 0, sizeof(*tab));
	strmap_init(&tab->syms, plain_cpy);
	tab->nr = tab->alloc = 0;
	tab->data = NULL;
}

void symtable_cpy(struct symtable *dst, struct symtable *src)
{
	memset(dst, 0, sizeof(*dst));
	strmap_cpy(&dst->syms, &src->syms);
	dst->nr = src->nr;
	dst->alloc = 0;
	ALLOC_GROW(dst->data, dst->nr, dst->alloc);
	for (size_t i = 0; i < dst->nr; i++)
		sym_data_cpy(&dst->data[i], &src->data[i]);
}

void symtable_destroy(struct symtable *tab)
{
	strmap_destroy(&tab->syms);
	FREE_AND_NULL(tab->data);
}

struct sym_data *symtable_find(struct symtable *tab, const char *symname)
{
	void *ret;
	if (strmap_find(&tab->syms, symname, &ret))
		return &tab->data[(size_t)ret];
	return NULL;
}

int symtable_has(struct symtable *tab, const char *symname)
{
	return strmap_has(&tab->syms, symname);
}

void symtable_put_lvar(struct symtable *tab, struct ast_var_decl *decl,
		       size_t stack_index, unsigned int scope)
{
	struct sym_data *sym = symtable_find(tab, decl->name);
	if (sym && sym->scope == scope) {
		die("redeclaration of variable '%s'. First:\n%s\nThen:\n%s",
		    decl->name, show_token_on_source_line(sym->tok),
		    show_token_on_source_line(decl->tok));
	} else if (!sym) {
		ALLOC_GROW(tab->data, tab->nr + 1, tab->alloc);
		sym = &tab->data[tab->nr];
		strmap_put(&tab->syms, decl->name, (void *)tab->nr);
		tab->nr++;
	}
	sym->type = SYM_LOCAL_VAR;
	sym->u.stack_index = stack_index;
	sym->tok = decl->tok;
	sym->scope = scope;
}

size_t symtable_var_ref(struct symtable *tab, struct var_ref *v)
{
	struct sym_data *sdata = symtable_find(tab, v->name);
	if (!sdata)
		die("Undeclared variable '%s'\n%s", v->name,
		    show_token_on_source_line(v->tok));
	return sdata->u.stack_index;
}
