#include "util.h"
#include "lib/strmap.h"
#include "lib/array.h"
#include "symtable.h"
#include "parser.h"
#include "lexer.h"

static void sym_data_cpy(struct sym_data *dst, struct sym_data *src)
{
	/* We can do a shallow copy for now. */
	*dst = *src;
}

void symtable_init(struct symtable *tab)
{
	memset(tab, 0, sizeof(*tab));
	strmap_init(&tab->syms, strmap_val_plain_copy);
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
		die("redefinition of symbol '%s'. First:\n%s\nThen:\n%s",
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

char *symtable_var_ref(struct symtable *tab, struct var_ref *v)
{
	struct sym_data *sdata = symtable_find(tab, v->name);
	if (!sdata)
		die("Undeclared variable '%s'\n%s", v->name,
		    show_token_on_source_line(v->tok));
	switch (sdata->type) {
	case SYM_LOCAL_VAR:
		return xmkstr("-%zu(%%rbp)", sdata->u.stack_index);
	case SYM_GLOBAL_VAR:
		return xmkstr("_var_%s(%%rip)", v->name);
	default:
		die("'%s' is not a variable\n%s", v->name,
		    show_token_on_source_line(v->tok));
	}
}

size_t symtable_bytes_in_scope(struct symtable *tab, unsigned int scope)
{
	size_t ret = 0;
	for (size_t i = 0; i < tab->nr; i++)
		if (tab->data[i].scope == scope)
			ret += 4; /* for now, all variables on the stack have size 4. */
	return ret;
}

void symtable_put_func(struct symtable *tab, struct ast_func_decl *decl,
		       unsigned int scope)
{
	struct sym_data *sym = symtable_find(tab, decl->name);
	if (sym && sym->type == SYM_FUNC) {
		/* All functions should be declared on scope 0. */
		assert(!sym->scope && !scope);
		if (sym->u.func->body && decl->body) {
			die("redefinition of function '%s'.\nFirst:\n%s\nThen:\n%s",
			    decl->name, show_token_on_source_line(sym->tok),
			    show_token_on_source_line(decl->tok));
		}
		if ((sym->u.func->return_type != decl->return_type) ||
		    (!sym->u.func->empty_parameter_declaration &&
		     !decl->empty_parameter_declaration &&
		     (sym->u.func->parameters.nr != decl->parameters.nr))) {
			/*
			 * NOTE: we can only do this direct comparison because
			 * all our parameters are int, and thus, same-sized.
			 */
			die("redeclaration of function '%s' with different signature.\nFirst:\n%s\nThen:\n%s",
			    decl->name, show_token_on_source_line(sym->tok),
			    show_token_on_source_line(decl->tok));
		}
		/*
		 * If we have a function with body already, keep that.
		 * But if the definition has an empty parameter declaration,
		 * the prototype must have too.
		 */
		if (sym->u.func->body) {
			if (sym->u.func->empty_parameter_declaration &&
			    !decl->empty_parameter_declaration) {
				die("redeclaration of function '%s' with different signature.\nFirst:\n%s\nThen:\n%s",
				    decl->name, show_token_on_source_line(sym->tok),
				    show_token_on_source_line(decl->tok));
			}
			return;
		}
	} else if (sym && sym->scope == scope) {
		die("redefinition of symbol '%s'.\nFirst:\n%s\nThen:\n%s",
		    decl->name, show_token_on_source_line(sym->tok),
		    show_token_on_source_line(decl->tok));
	} else if (!sym) {
		ALLOC_GROW(tab->data, tab->nr + 1, tab->alloc);
		sym = &tab->data[tab->nr];
		strmap_put(&tab->syms, decl->name, (void *)tab->nr);
		tab->nr++;
	}
	sym->type = SYM_FUNC;
	sym->u.func = decl;
	sym->tok = decl->tok;
	sym->scope = scope;
}

struct ast_func_decl *symtable_func_call(struct symtable *tab,
					 struct func_call *call)
{
	struct sym_data *sdata = symtable_find(tab, call->name);
	if (!sdata)
		die("call to undeclared function '%s'\n%s", call->name,
		    show_token_on_source_line(call->tok));
	if (sdata->type != SYM_FUNC)
		die("cannot call '%s': it is not a function\n%s\nDefined here:\n%s",
		    call->name, show_token_on_source_line(call->tok),
		    show_token_on_source_line(sdata->tok));
	if (!sdata->u.func->empty_parameter_declaration &&
	    (sdata->u.func->parameters.nr != call->args.nr))
		die("parameter mismatch on call to '%s'\n%s\nDefined here:\n%s",
		    call->name, show_token_on_source_line(call->tok),
		    show_token_on_source_line(sdata->tok));
	return sdata->u.func;
}

char *symtable_put_gvar(struct symtable *tab, struct ast_var_decl *decl)
{
	struct sym_data *sym = symtable_find(tab, decl->name);
	if (sym) {
		if (sym->scope)
			BUG("symtable: found symbol with non-zero scope"
			    " while adding global var");

		if (sym->type != SYM_GLOBAL_VAR || (decl->value && sym->u.gvar->value))
			die("redefinition of symbol '%s'. First:\n%s\nThen:\n%s",
			    decl->name, show_token_on_source_line(sym->tok),
			    show_token_on_source_line(decl->tok));

		if (sym->u.gvar->value || !decl->value)
			goto out;
	} else {
		ALLOC_GROW(tab->data, tab->nr + 1, tab->alloc);
		sym = &tab->data[tab->nr];
		strmap_put(&tab->syms, decl->name, (void *)tab->nr);
		tab->nr++;
	}
	sym->type = SYM_GLOBAL_VAR;
	sym->u.gvar = decl;
	sym->tok = decl->tok;
	sym->scope = 0;
out:
	return xmkstr("_var_%s", decl->name);
}

void foreach_uninitialized_gvar(struct symtable *tab,
				void (*fn)(char *, void *), void *data) {
	for (size_t i = 0; i < tab->nr; i++) {
		if (tab->data[i].type != SYM_GLOBAL_VAR)
			continue;
		struct ast_var_decl *var = tab->data[i].u.gvar;
		if (!var->value) {
			char *var_label = xmkstr("_var_%s", var->name);
			fn(var_label, data);
			free(var_label);
		}
	}
}
