/*
 * ast.c -- Building, printing and freeing the Abstract Syntax Tree.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"

/* ================================================================
 * Memory helpers
 * ================================================================ */

/* malloc() that never returns NULL: running out of memory is fatal. */
void *xmalloc(size_t size)
{
    void *p = malloc(size);
    if (p == NULL) {
        fprintf(stderr, "fatal: out of memory\n");
        exit(2);
    }
    return p;
}

char *xstrdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *copy = xmalloc(len);
    memcpy(copy, s, len);
    return copy;
}

/* Allocates a node of the given kind with all other fields zeroed. */
static Node *new_node(NodeKind kind, int line, int col)
{
    Node *n = xmalloc(sizeof *n);
    memset(n, 0, sizeof *n);
    n->kind = kind;
    n->line = line;
    n->col  = col;
    n->type = TYPE_ERROR;   /* unknown until Phase 3 type checking */
    return n;
}

/* ================================================================
 * Lists
 * ================================================================ */

NodeList *list_new(void)
{
    NodeList *list = xmalloc(sizeof *list);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
    return list;
}

/*
 * Appends a node and returns the list (so it can be used directly in parser
 * actions). NULL nodes are skipped: the parser uses NULL for a statement or
 * declaration that had a syntax error and was thrown away.
 */
NodeList *list_append(NodeList *list, Node *node)
{
    if (node == NULL) {
        return list;
    }
    if (list->count == list->capacity) {
        list->capacity = list->capacity == 0 ? 4 : list->capacity * 2;
        list->items = realloc(list->items, list->capacity * sizeof *list->items);
        if (list->items == NULL) {
            fprintf(stderr, "fatal: out of memory\n");
            exit(2);
        }
    }
    list->items[list->count++] = node;
    return list;
}

/* ================================================================
 * Constructors
 * ================================================================ */

Node *ast_program(NodeList *decls)
{
    Node *n = new_node(N_PROGRAM, 1, 1);
    n->u.program.decls = decls;
    return n;
}

Node *ast_var_decl(int line, int col, Type type, char *name, Node *init)
{
    Node *n = new_node(N_VAR_DECL, line, col);
    n->u.var_decl.type = type;
    n->u.var_decl.name = name;
    n->u.var_decl.init = init;
    return n;
}

Node *ast_func_decl(int line, int col, Type ret_type, char *name,
                    NodeList *params, Node *body)
{
    Node *n = new_node(N_FUNC_DECL, line, col);
    n->u.func_decl.ret_type = ret_type;
    n->u.func_decl.name = name;
    n->u.func_decl.params = params;
    n->u.func_decl.body = body;
    return n;
}

Node *ast_param(int line, int col, Type type, char *name)
{
    Node *n = new_node(N_PARAM, line, col);
    n->u.param.type = type;
    n->u.param.name = name;
    return n;
}

Node *ast_block(int line, int col, NodeList *stmts)
{
    Node *n = new_node(N_BLOCK, line, col);
    n->u.block.stmts = stmts;
    return n;
}

Node *ast_assign(int line, int col, char *name, Node *value)
{
    Node *n = new_node(N_ASSIGN, line, col);
    n->u.assign.name = name;
    n->u.assign.value = value;
    return n;
}

Node *ast_if(int line, int col, Node *cond, Node *then_stmt, Node *else_stmt)
{
    Node *n = new_node(N_IF, line, col);
    n->u.if_stmt.cond = cond;
    n->u.if_stmt.then_stmt = then_stmt;
    n->u.if_stmt.else_stmt = else_stmt;
    return n;
}

Node *ast_while(int line, int col, Node *cond, Node *body)
{
    Node *n = new_node(N_WHILE, line, col);
    n->u.while_stmt.cond = cond;
    n->u.while_stmt.body = body;
    return n;
}

Node *ast_for(int line, int col, Node *init, Node *cond, Node *update, Node *body)
{
    Node *n = new_node(N_FOR, line, col);
    n->u.for_stmt.init = init;
    n->u.for_stmt.cond = cond;
    n->u.for_stmt.update = update;
    n->u.for_stmt.body = body;
    return n;
}

Node *ast_return(int line, int col, Node *value)
{
    Node *n = new_node(N_RETURN, line, col);
    n->u.return_stmt.value = value;
    return n;
}

Node *ast_print(int line, int col, Node *value)
{
    Node *n = new_node(N_PRINT, line, col);
    n->u.print_stmt.value = value;
    return n;
}

Node *ast_expr_stmt(int line, int col, Node *expr)
{
    Node *n = new_node(N_EXPR_STMT, line, col);
    n->u.expr_stmt.expr = expr;
    return n;
}

Node *ast_binary(int line, int col, BinOp op, Node *left, Node *right)
{
    Node *n = new_node(N_BINARY, line, col);
    n->u.binary.op = op;
    n->u.binary.left = left;
    n->u.binary.right = right;
    return n;
}

Node *ast_unary(int line, int col, UnOp op, Node *operand)
{
    Node *n = new_node(N_UNARY, line, col);
    n->u.unary.op = op;
    n->u.unary.operand = operand;
    return n;
}

Node *ast_int(int line, int col, int value)
{
    Node *n = new_node(N_INT_LIT, line, col);
    n->u.int_val = value;
    return n;
}

Node *ast_float(int line, int col, double value)
{
    Node *n = new_node(N_FLOAT_LIT, line, col);
    n->u.float_val = value;
    return n;
}

Node *ast_bool(int line, int col, int value)
{
    Node *n = new_node(N_BOOL_LIT, line, col);
    n->u.bool_val = value;
    return n;
}

Node *ast_var(int line, int col, char *name)
{
    Node *n = new_node(N_VAR, line, col);
    n->u.var.name = name;
    return n;
}

Node *ast_call(int line, int col, char *name, NodeList *args)
{
    Node *n = new_node(N_CALL, line, col);
    n->u.call.name = name;
    n->u.call.args = args;
    return n;
}

/* ================================================================
 * Names for printing
 * ================================================================ */

const char *type_name(Type t)
{
    switch (t) {
    case TYPE_INT:   return "int";
    case TYPE_FLOAT: return "float";
    case TYPE_BOOL:  return "bool";
    case TYPE_VOID:  return "void";
    case TYPE_ERROR: return "<error>";
    }
    return "?";
}

const char *binop_name(BinOp op)
{
    switch (op) {
    case BIN_ADD: return "+";
    case BIN_SUB: return "-";
    case BIN_MUL: return "*";
    case BIN_DIV: return "/";
    case BIN_MOD: return "%";
    case BIN_EQ:  return "==";
    case BIN_NE:  return "!=";
    case BIN_LT:  return "<";
    case BIN_LE:  return "<=";
    case BIN_GT:  return ">";
    case BIN_GE:  return ">=";
    case BIN_AND: return "&&";
    case BIN_OR:  return "||";
    case BIN_SHL: return "<<";
    }
    return "?";
}

const char *unop_name(UnOp op)
{
    return op == UN_NEG ? "-" : "!";
}

/*
 * Formats a float so it always looks like a float: 2.0 is printed as "2.0",
 * not "2", so it cannot be confused with the integer 2.
 */
void format_float(double v, char *buf, int size)
{
    snprintf(buf, size, "%g", v);
    if (strpbrk(buf, ".eEn") == NULL) {     /* no '.', exponent, inf or nan */
        strncat(buf, ".0", size - strlen(buf) - 1);
    }
}

/* ================================================================
 * Printing (--ast)
 *
 * Each node is printed on its own line, indented two spaces per level.
 * Where a child has a specific role (the condition of an if, the body of a
 * loop, ...), the line starts with that role, e.g. "cond: Binary <".
 * Declarations and statements also show their source line.
 * ================================================================ */

static void print_node(const Node *n, int depth, const char *role);

static void indent(int depth)
{
    for (int i = 0; i < depth; i++) {
        printf("  ");
    }
}

/* Prints one line: indentation, optional role, then the text. */
static void print_line(int depth, const char *role, const char *text)
{
    indent(depth);
    if (role != NULL) {
        printf("%s: ", role);
    }
    printf("%s\n", text);
}

static void print_list(const NodeList *list, int depth)
{
    if (list == NULL) {
        return;
    }
    for (int i = 0; i < list->count; i++) {
        print_node(list->items[i], depth, NULL);
    }
}

static void print_node(const Node *n, int depth, const char *role)
{
    char text[256];

    /* An optional part that is missing, e.g. an empty for-loop condition. */
    if (n == NULL) {
        print_line(depth, role, "(none)");
        return;
    }

    switch (n->kind) {
    case N_PROGRAM:
        print_line(depth, role, "Program");
        print_list(n->u.program.decls, depth + 1);
        break;

    case N_VAR_DECL:
        snprintf(text, sizeof text, "VarDecl %s %s  [line %d]",
                 type_name(n->u.var_decl.type), n->u.var_decl.name, n->line);
        print_line(depth, role, text);
        if (n->u.var_decl.init != NULL) {
            print_node(n->u.var_decl.init, depth + 1, "init");
        }
        break;

    case N_FUNC_DECL:
        snprintf(text, sizeof text, "FuncDecl %s %s  [line %d]",
                 type_name(n->u.func_decl.ret_type), n->u.func_decl.name, n->line);
        print_line(depth, role, text);
        if (n->u.func_decl.params->count == 0) {
            print_line(depth + 1, "params", "(none)");
        } else {
            print_line(depth + 1, NULL, "params:");
            print_list(n->u.func_decl.params, depth + 2);
        }
        print_node(n->u.func_decl.body, depth + 1, "body");
        break;

    case N_PARAM:
        snprintf(text, sizeof text, "Param %s %s",
                 type_name(n->u.param.type), n->u.param.name);
        print_line(depth, role, text);
        break;

    case N_BLOCK:
        snprintf(text, sizeof text, "Block  [line %d]", n->line);
        print_line(depth, role, text);
        print_list(n->u.block.stmts, depth + 1);
        break;

    case N_ASSIGN:
        snprintf(text, sizeof text, "Assign %s  [line %d]",
                 n->u.assign.name, n->line);
        print_line(depth, role, text);
        print_node(n->u.assign.value, depth + 1, NULL);
        break;

    case N_IF:
        snprintf(text, sizeof text, "If  [line %d]", n->line);
        print_line(depth, role, text);
        print_node(n->u.if_stmt.cond, depth + 1, "cond");
        print_node(n->u.if_stmt.then_stmt, depth + 1, "then");
        if (n->u.if_stmt.else_stmt != NULL) {
            print_node(n->u.if_stmt.else_stmt, depth + 1, "else");
        }
        break;

    case N_WHILE:
        snprintf(text, sizeof text, "While  [line %d]", n->line);
        print_line(depth, role, text);
        print_node(n->u.while_stmt.cond, depth + 1, "cond");
        print_node(n->u.while_stmt.body, depth + 1, "body");
        break;

    case N_FOR:
        snprintf(text, sizeof text, "For  [line %d]", n->line);
        print_line(depth, role, text);
        print_node(n->u.for_stmt.init, depth + 1, "init");
        print_node(n->u.for_stmt.cond, depth + 1, "cond");
        print_node(n->u.for_stmt.update, depth + 1, "update");
        print_node(n->u.for_stmt.body, depth + 1, "body");
        break;

    case N_RETURN:
        snprintf(text, sizeof text, "Return  [line %d]", n->line);
        print_line(depth, role, text);
        if (n->u.return_stmt.value != NULL) {
            print_node(n->u.return_stmt.value, depth + 1, NULL);
        }
        break;

    case N_PRINT:
        snprintf(text, sizeof text, "Print  [line %d]", n->line);
        print_line(depth, role, text);
        print_node(n->u.print_stmt.value, depth + 1, NULL);
        break;

    case N_EXPR_STMT:
        snprintf(text, sizeof text, "ExprStmt  [line %d]", n->line);
        print_line(depth, role, text);
        print_node(n->u.expr_stmt.expr, depth + 1, NULL);
        break;

    case N_BINARY:
        snprintf(text, sizeof text, "Binary %s", binop_name(n->u.binary.op));
        print_line(depth, role, text);
        print_node(n->u.binary.left, depth + 1, NULL);
        print_node(n->u.binary.right, depth + 1, NULL);
        break;

    case N_UNARY:
        snprintf(text, sizeof text, "Unary %s", unop_name(n->u.unary.op));
        print_line(depth, role, text);
        print_node(n->u.unary.operand, depth + 1, NULL);
        break;

    case N_INT_LIT:
        snprintf(text, sizeof text, "Int %d", n->u.int_val);
        print_line(depth, role, text);
        break;

    case N_FLOAT_LIT: {
        char num[64];
        format_float(n->u.float_val, num, sizeof num);
        snprintf(text, sizeof text, "Float %s", num);
        print_line(depth, role, text);
        break;
    }

    case N_BOOL_LIT:
        snprintf(text, sizeof text, "Bool %s", n->u.bool_val ? "true" : "false");
        print_line(depth, role, text);
        break;

    case N_VAR:
        snprintf(text, sizeof text, "Var %s", n->u.var.name);
        print_line(depth, role, text);
        break;

    case N_CALL:
        snprintf(text, sizeof text, "Call %s", n->u.call.name);
        print_line(depth, role, text);
        if (n->u.call.args->count > 0) {
            print_line(depth + 1, NULL, "args:");
            print_list(n->u.call.args, depth + 2);
        }
        break;
    }
}

void ast_print_tree(const Node *root)
{
    print_node(root, 0, NULL);
}

/* ================================================================
 * Freeing
 * ================================================================ */

void list_free(NodeList *list)
{
    if (list == NULL) {
        return;
    }
    for (int i = 0; i < list->count; i++) {
        ast_free(list->items[i]);
    }
    free(list->items);
    free(list);
}

void ast_free(Node *n)
{
    if (n == NULL) {
        return;
    }

    switch (n->kind) {
    case N_PROGRAM:   list_free(n->u.program.decls); break;
    case N_VAR_DECL:  free(n->u.var_decl.name); ast_free(n->u.var_decl.init); break;
    case N_FUNC_DECL: free(n->u.func_decl.name);
                      list_free(n->u.func_decl.params);
                      ast_free(n->u.func_decl.body); break;
    case N_PARAM:     free(n->u.param.name); break;
    case N_BLOCK:     list_free(n->u.block.stmts); break;
    case N_ASSIGN:    free(n->u.assign.name); ast_free(n->u.assign.value); break;
    case N_IF:        ast_free(n->u.if_stmt.cond);
                      ast_free(n->u.if_stmt.then_stmt);
                      ast_free(n->u.if_stmt.else_stmt); break;
    case N_WHILE:     ast_free(n->u.while_stmt.cond);
                      ast_free(n->u.while_stmt.body); break;
    case N_FOR:       ast_free(n->u.for_stmt.init);
                      ast_free(n->u.for_stmt.cond);
                      ast_free(n->u.for_stmt.update);
                      ast_free(n->u.for_stmt.body); break;
    case N_RETURN:    ast_free(n->u.return_stmt.value); break;
    case N_PRINT:     ast_free(n->u.print_stmt.value); break;
    case N_EXPR_STMT: ast_free(n->u.expr_stmt.expr); break;
    case N_BINARY:    ast_free(n->u.binary.left); ast_free(n->u.binary.right); break;
    case N_UNARY:     ast_free(n->u.unary.operand); break;
    case N_VAR:       free(n->u.var.name); break;
    case N_CALL:      free(n->u.call.name); list_free(n->u.call.args); break;
    case N_INT_LIT:
    case N_FLOAT_LIT:
    case N_BOOL_LIT:  break;
    }
    free(n);
}
