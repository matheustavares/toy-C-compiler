#ifndef _STACK_H
#define _STACK_H

#include "array.h"

struct stack {
	void **stack;
	size_t nr, alloc;
};

#define STACK_INITIALIZER { NULL, 0, 0 }

static void stack_push(struct stack *s, void *value)
{
	ALLOC_GROW(s->stack, s->nr + 1, s->alloc);
	s->stack[s->nr++] = value;
}

static void *stack_pop(struct stack *s)
{
	assert(s->nr);
	return s->stack[--s->nr];
}

static void *stack_peek(struct stack *s)
{
	assert(s->nr);
	return s->stack[s->nr - 1];
}

#define stack_size(s) ((s)->nr)
#define stack_empty(s) (!stack_size(s))

#endif
