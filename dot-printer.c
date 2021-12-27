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
	case EXP_OP_PREFIX_INC: return "prefix ++";
	case EXP_OP_PREFIX_DEC: return "prefix --";
	case EXP_OP_SUFFIX_INC: return "suffix ++";
	case EXP_OP_SUFFIX_DEC: return "suffix --";
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
	case EXP_OP_MODULO: return "%";

	case EXP_OP_LOGIC_AND: return "&&";
	case EXP_OP_LOGIC_OR: return "||";
	case EXP_OP_EQUAL: return "==";
	case EXP_OP_NOT_EQUAL: return "!=";
	case EXP_OP_LT: return "<";
	case EXP_OP_LE: return "<=";
	case EXP_OP_GT: return ">";
	case EXP_OP_GE: return ">=";

	case EXP_OP_BITWISE_AND: return "&";
	case EXP_OP_BITWISE_OR: return "|";
	case EXP_OP_BITWISE_XOR: return "^";
	case EXP_OP_BITWISE_LEFT_SHIFT: return "<<";
	case EXP_OP_BITWISE_RIGHT_SHIFT: return ">>";

	case EXP_OP_ASSIGNMENT: return "Assignment";

	default: die("BUG: unknown bin_op type %d", type);
	}
}

/* TODO: would love to unify these two. */
#define print_arc(from, to) printf(" %zu -> %zu;\n", from, to)
#define print_arc_label(from, to, label) \
	printf(" %zu -> %zu [label=\"%s\"];\n", from, to, label)

static size_t print_ast_expression(struct ast_expression *exp, struct label_list *labels)
{
	const char *type_str;
	size_t node, next_node;

	switch (exp->type) {
	case AST_EXP_BINARY_OP:
		type_str = bin_op_as_str(exp->u.bin_op.type);
		node = add_label(labels, xmkstr("Binary op: '%s'", type_str));
		next_node = print_ast_expression(exp->u.bin_op.lexp, labels);
		print_arc(node, next_node);
		next_node = print_ast_expression(exp->u.bin_op.rexp, labels);
		print_arc(node, next_node);
		break;
	case AST_EXP_UNARY_OP:
		type_str = un_op_as_str(exp->u.un_op.type);
		node = add_label(labels, xmkstr("Unary op: '%s'", type_str));
		next_node = print_ast_expression(exp->u.un_op.exp, labels);
		print_arc(node, next_node);
		break;
	case AST_EXP_CONSTANT_INT:
		node = add_label(labels, xmkstr("Constant int: '%d'", exp->u.ival));
		break;
	case AST_EXP_VAR:
		node = add_label(labels, xmkstr("Variable '%s'", exp->u.var.name));
		break;
	default:
		die("BUG: unknown ast expression type: %d", exp->type);
	}

	return node;
}

static size_t print_ast_statement(struct ast_statement *st, struct label_list *labels)
{
	size_t node, next_node;
	switch (st->type) {
	case AST_ST_RETURN:
		node = add_label(labels, xstrdup("Return"));
		next_node = print_ast_expression(st->u.ret_exp, labels);
		print_arc(node, next_node);
		break;
	case AST_ST_VAR_DECL:
		node = add_label(labels, xmkstr("Declare variable '%s'", st->u.decl->name));
		if (st->u.decl->value) {
			next_node = print_ast_expression(st->u.decl->value, labels);
			print_arc_label(node, next_node, "with\nvalue");
		}
		break;
	case AST_ST_EXPRESSION:
		node = print_ast_expression(st->u.exp, labels);
		break;
	case AST_ST_IF_ELSE:
		node = add_label(labels, xstrdup("if"));
		next_node = print_ast_expression(st->u.if_else.condition, labels);
		print_arc_label(node, next_node, "condition");
		next_node = print_ast_statement(st->u.if_else.if_st, labels);
		print_arc_label(node, next_node, "then");
		if (st->u.if_else.else_st) {
			next_node = print_ast_statement(st->u.if_else.else_st, labels);
			print_arc_label(node, next_node, "else");
		}
		break;
	default:
		die("BUG: unknown ast statement type: %d", st->type);
	}
	return node;
}

static size_t print_ast_func_decl(struct ast_func_decl *fun, struct label_list *labels)
{
	size_t node = add_label(labels, xmkstr("Function: %s", fun->name));
	for (struct ast_statement *st = fun->body; st; st = st->next) {
		size_t next_node = print_ast_statement(st, labels);
		print_arc(node, next_node);
	}
	return node;
}

static void print_ast_program(struct ast_program *prog, struct label_list *labels)
{
	size_t node = add_label(labels, xstrdup("Program"));
	size_t next_node = print_ast_func_decl(prog->fun, labels);
	print_arc(node, next_node);
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
