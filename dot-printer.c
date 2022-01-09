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
	case AST_EXP_TERNARY:
		node = add_label(labels, xstrdup("Ternary op (?:)"));
		next_node = print_ast_expression(exp->u.ternary.condition, labels);
		print_arc_label(node, next_node, "condition");
		next_node = print_ast_expression(exp->u.ternary.if_exp, labels);
		print_arc_label(node, next_node, "then");
		next_node = print_ast_expression(exp->u.ternary.else_exp, labels);
		print_arc_label(node, next_node, "else");
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
	case AST_EXP_FUNC_CALL:
		node = add_label(labels, xmkstr("Call '%s'", exp->u.call.name));
		for (size_t i = 0; i < exp->u.call.args.nr; i++) {
			next_node = print_ast_expression(exp->u.call.args.arr[i], labels);
			char *arg = xmkstr("arg %zu", i);
			print_arc_label(node, next_node, arg);
			free(arg);
		}
		break;
	default:
		die("BUG: unknown ast expression type: %d", exp->type);
	}

	return node;
}

static size_t print_ast_var_decl(struct ast_var_decl *decl, struct label_list *labels)
{
	size_t node = add_label(labels, xmkstr("Declare variable '%s'", decl->name));
	if (decl->value) {
		size_t value_node = print_ast_expression(decl->value, labels);
		print_arc_label(node, value_node, "with\\nvalue");
	}
	return node;
}

static size_t print_ast_opt_expression(struct ast_opt_expression opt_exp,
				       struct label_list *labels)
{
	return opt_exp.exp ? print_ast_expression(opt_exp.exp, labels) :
	       add_label(labels, xstrdup("null expression"));
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
		node = print_ast_var_decl(st->u.decl, labels);
		break;
	case AST_ST_EXPRESSION:
		node = print_ast_opt_expression(st->u.opt_exp, labels);
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
	case AST_ST_BLOCK:
		node = add_label(labels, xstrdup("Block"));
		for (size_t i = 0; i < st->u.block.nr; i++) {
			struct ast_statement *item = st->u.block.items[i];
			size_t item_node = print_ast_statement(item, labels);
			print_arc(node, item_node);
		}
		break;

	case AST_ST_FOR:
		node = add_label(labels, xstrdup("for"));
		next_node = print_ast_opt_expression(st->u._for.prologue, labels);
		print_arc_label(node, next_node, "prologue");
		next_node = print_ast_expression(st->u._for.condition, labels);
		print_arc_label(node, next_node, "condition");
		next_node = print_ast_opt_expression(st->u._for.epilogue, labels);
		print_arc_label(node, next_node, "epilogue");
		next_node = print_ast_statement(st->u._for.body, labels);
		print_arc_label(node, next_node, "body");
		break;
	case AST_ST_FOR_DECL:
		node = add_label(labels, xstrdup("for"));
		next_node = print_ast_var_decl(st->u.for_decl.decl, labels);
		print_arc_label(node, next_node, "prologue");
		next_node = print_ast_expression(st->u.for_decl.condition, labels);
		print_arc_label(node, next_node, "condition");
		next_node = print_ast_opt_expression(st->u.for_decl.epilogue, labels);
		print_arc_label(node, next_node, "epilogue");
		next_node = print_ast_statement(st->u.for_decl.body, labels);
		print_arc_label(node, next_node, "body");
		break;
	case AST_ST_WHILE:
		node = add_label(labels, xstrdup("while"));
		next_node = print_ast_expression(st->u._while.condition, labels);
		print_arc_label(node, next_node, "condition");
		next_node = print_ast_statement(st->u._while.body, labels);
		print_arc_label(node, next_node, "body");
		break;
	case AST_ST_DO:
		node = add_label(labels, xstrdup("do"));
		next_node = print_ast_statement(st->u._do.body, labels);
		print_arc_label(node, next_node, "body");
		next_node = print_ast_expression(st->u._do.condition, labels);
		print_arc_label(node, next_node, "condition");
		break;
	case AST_ST_BREAK:
		node = add_label(labels, xstrdup("<break> keyword"));
		break;
	case AST_ST_CONTINUE:
		node = add_label(labels, xstrdup("<continue> keyword"));
		break;

	case AST_ST_GOTO:
		node = add_label(labels, xmkstr("goto '%s'", st->u._goto.label));
		break;
	case AST_ST_LABELED_STATEMENT:
		node = add_label(labels, xmkstr("label '%s'", st->u.labeled_st.label));
		next_node = print_ast_statement(st->u.labeled_st.st, labels);
		print_arc_label(node, next_node, "statement");
		break;

	default:
		die("BUG: unknown ast statement type: %d", st->type);
	}
	return node;
}

static size_t print_ast_func_decl(struct ast_func_decl *fun, struct label_list *labels)
{
	size_t node = add_label(labels, xmkstr("Function: %s", fun->name));
	for (size_t i = 0; i < fun->parameters.nr; i++) {
		size_t next_node = add_label(labels, xstrdup(fun->parameters.arr[i]));
		char *parameter = xmkstr("parameter %zu", i);
		print_arc_label(node, next_node, parameter);
		free(parameter);
	}
	if (fun->body) {
		size_t next_node = print_ast_statement(fun->body, labels);
		print_arc_label(node, next_node, "body");
	}
	return node;
}

static void print_ast_program(struct ast_program *prog, struct label_list *labels)
{
	size_t node = add_label(labels, xstrdup("Program"));
	for (size_t i = 0; i < prog->funcs.nr; i++) {
		size_t next_node = print_ast_func_decl(prog->funcs.arr[i], labels);
		print_arc(node, next_node);
	}
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
