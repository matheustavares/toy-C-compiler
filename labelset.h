#ifndef _LABELSET_H
#define _LABELSET_H

#include "lib/strmap.h"

struct token;

struct labelset {
	struct strmap map;
};

void labelset_init(struct labelset *set);
void labelset_destroy(struct labelset *set);

void labelset_put_reference(struct labelset *set, const char *label,
			    struct token *tok);
void labelset_put_definition(struct labelset *set, const char *label,
			     struct token *tok);

void labelset_check(struct labelset *set);

#endif
