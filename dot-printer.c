#include "util.h"
#include "dot-printer.h"
#include "parser.h"

/*******************************************************************************
 *		    Routines to print ASTs in dot format
*******************************************************************************/

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

static void print_ast_expression(struct ast_expression *exp)
{
	const char *type_str;
	switch (exp->type) {
	case AST_EXP_BINARY_OP:
		type_str = bin_op_as_str(exp->u.bin_op.type);
		printf("\"Binary Op '%s'\";\n", type_str);
		printf("  \"Binary Op '%s'\" -> ", type_str);
		print_ast_expression(exp->u.bin_op.lexp);
		printf("  \"Binary Op '%s'\" -> ", type_str);
		print_ast_expression(exp->u.bin_op.rexp);
		break;
	case AST_EXP_UNARY_OP:
		type_str = un_op_as_str(exp->u.un_op.type);
		printf("\"Unary Op '%s'\";\n", type_str);
		printf("  \"Unary Op '%s'\" -> ", type_str);
		print_ast_expression(exp->u.un_op.exp);
		break;
	case AST_EXP_CONSTANT_INT:
		printf("\"Constant int '%d'\";\n", exp->u.ival);
		break;
	default:
		die("BUG: unknown ast expression type: %d", exp->type);
	}
}

static void print_ast_statement(struct ast_statement *st)
{
	switch (st->type) {
	case AST_ST_RETURN:
		printf("Return;\n");
		printf("  Return -> ");
		print_ast_expression(st->u.ret_exp);
		break;
	default:
		die("BUG: unknown ast statement type: %d", st->type);
	}
}

static void print_ast_func_decl (struct ast_func_decl *fun)
{
	printf("\"Function: %s\";\n", fun->name);
	printf("  \"Function: %s\" -> ", fun->name);
	print_ast_statement(fun->body);
}

static void print_ast_program(struct ast_program *prog)
{
	printf("  Program -> ");
	print_ast_func_decl(prog->fun);
}

void print_ast_in_dot(struct ast_program *prog)
{
	printf("strict digraph {\n");
	print_ast_program(prog);
	printf("}\n");
}
