#include "util.h"
#include "labelset.h"
#include "lexer.h"

struct label_info {
	enum { LABEL_REFERENCED, LABEL_DEFINED } status;
	struct token *tok;
};

void labelset_init(struct labelset *set)
{
	strmap_init(&set->map, strmap_val_plain_copy);
}

void labelset_put_reference(struct labelset *set, const char *label,
			    struct token *tok)
{
	struct label_info *label_info = xmalloc(sizeof(*label_info));
	label_info->status = LABEL_REFERENCED;
	label_info->tok = tok;
	if (!strmap_has(&set->map, label))
		strmap_put(&set->map, label, label_info);
}

void labelset_put_definition(struct labelset *set, const char *label,
			     struct token *tok)
{
	struct label_info *label_info;
	if (strmap_find(&set->map, label, (void **)&label_info)) {
		if (label_info->status == LABEL_DEFINED) {
			die("generate x86: redefinition of label '%s'.\nFirst:\n%s\nThen:\n%s",
			    label, show_token_on_source_line(label_info->tok),
			    show_token_on_source_line(tok));
		} else {
			label_info->status = LABEL_DEFINED;
			label_info->tok = tok;
		}
	} else {
		label_info = xmalloc(sizeof(*label_info));
		label_info->status = LABEL_DEFINED;
		label_info->tok = tok;
		strmap_put(&set->map, label, label_info);
	}
}

static int check_label_info_defined(const char *label, void *val, void *_)
{
	struct label_info *label_info = val;
	if (label_info->status != LABEL_DEFINED) {
		die("generate x86: unknown label '%s'.\n%s", label,
		    show_token_on_source_line(label_info->tok));
	}
	return 0;
}

void labelset_check(struct labelset *set)
{
	strmap_iterate(&set->map, check_label_info_defined, NULL);
}

static int free_label_info(const char *label, void *val, void *_)
{
	free(val);
	return 0;
}

void labelset_destroy(struct labelset *set)
{
	strmap_iterate(&set->map, free_label_info, NULL);
	strmap_destroy(&set->map);
}
