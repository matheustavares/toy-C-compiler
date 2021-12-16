#include "util.h"
#include "dot-printer.h"
#include "parser.h"
#include "lib/array.h"

/*******************************************************************************
 *		    Routines to print ASTs in dot format
*******************************************************************************/

/* The label to a dot node. */
struct label_list {
	char **arr;
	size_t nr, alloc;
};

#define LABEL_LIST_INIT { NULL, 0, 0 }

static size_t add_label(struct label_list *list, char *label)
{
	ALLOC_GROW(list->arr, list->nr + 1, list->alloc);
	list->arr[list->nr] = label;
	return list->nr++;
}

static void print_labels(struct label_list *labels)
{
	for (size_t i = 0; i < labels->nr; i++)
		printf("  %zu [label=\"%s\"];\n", i, labels->arr[i]);
}

static void free_labels(struct label_list *labels)
{
	for (size_t i = 0; i < labels->nr; i++)
		free(labels->arr[i]);
}

static const char *un_op_as_str(enum un_op_type type)
{
	switch (type) {
	case EXP_OP_NEGATION: return "-";
	case EXP_OP_BIT_COMPLEMENT: return "~";
	case EXP_OP_LOGIC_NEGATION: return "!";
	default: die("BUG: unknown un_op type %d", type);
	}
}

static const char *bin_op_as_str(enum bin_op_type type)
{
	switch (type) {
	case EXP_OP_ADDITION: return "+";
	case EXP_OP_SUBTRACTION: return "-";
	case EXP_OP_DIVISION: return "/";
	case EXP_OP_MULTIPLICATION: return "*";
	default: die("BUG: unknown bin_op type %d", type);
	}
}

#define print_arc_start(id) printf(" %zu -> ", id)
#define print_arc_end(id) printf("%zu;\n", id)

static void print_ast_expression(struct ast_expression *exp, struct label_list *labels)
{
	const char *type_str;
	size_t node_id;

	switch (exp->type) {
	case AST_EXP_BINARY_OP:
		type_str = bin_op_as_str(exp->u.bin_op.type);
		node_id = add_label(labels, xmkstr("Binary op: '%s'", type_str));
		print_arc_end(node_id);
		print_arc_start(node_id);
		print_ast_expression(exp->u.bin_op.lexp, labels);
		print_arc_start(node_id);
		print_ast_expression(exp->u.bin_op.rexp, labels);
		break;
	case AST_EXP_UNARY_OP:
		type_str = un_op_as_str(exp->u.un_op.type);
		node_id = add_label(labels, xmkstr("Unary op: '%s'", type_str));
		print_arc_end(node_id);
		print_arc_start(node_id);
		print_ast_expression(exp->u.un_op.exp, labels);
		break;
	case AST_EXP_CONSTANT_INT:
		node_id = add_label(labels, xmkstr("Constant int: '%d'", exp->u.ival));
		print_arc_end(node_id);
		break;
	default:
		die("BUG: unknown ast expression type: %d", exp->type);
	}
}

static void print_ast_statement(struct ast_statement *st, struct label_list *labels)
{
	size_t node_id;
	switch (st->type) {
	case AST_ST_RETURN:
		node_id = add_label(labels, xstrdup("Return"));
		print_arc_end(node_id);
		print_arc_start(node_id);
		print_ast_expression(st->u.ret_exp, labels);
		break;
	default:
		die("BUG: unknown ast statement type: %d", st->type);
	}
}

static void print_ast_func_decl (struct ast_func_decl *fun, struct label_list *labels)
{
	size_t node_id = add_label(labels, xmkstr("Function: %s", fun->name));
	print_arc_end(node_id);
	print_arc_start(node_id);
	print_ast_statement(fun->body, labels);
}

static void print_ast_program(struct ast_program *prog, struct label_list *labels)
{
	size_t node_id = add_label(labels, xstrdup("Program"));
	print_arc_start(node_id);
	print_ast_func_decl(prog->fun, labels);
}

void print_ast_in_dot(struct ast_program *prog)
{
	struct label_list labels = LABEL_LIST_INIT;
	printf("strict digraph {\n");
	print_ast_program(prog, &labels);
	printf("\n");
	print_labels(&labels);
	printf("}\n");
	free_labels(&labels);
}
