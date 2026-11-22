/*
 * semantic.c -- Phase 3: semantic analysis (scopes and type checking).
 *
 * The analysis makes two passes over the global declarations:
 *
 *   Pass 1  Enter every global name into the global scope, in source
 *           order: global variables with their types, and functions with
 *           their signatures. Redeclared global names are reported here.
 *           So all globals are visible in the whole program: a function
 *           can call itself, or a function defined later in the file.
 *   Pass 2  Check the global initializers and the function bodies.
 *
 * Inside a function, statements are checked in order. A variable can only
 * be used after its declaration, and each { } block opens a new scope.
 *
 * Type rules
 *   + - * /         int or float operands. The result is float if either
 *                   operand is float, otherwise int.
 *   %               int operands, int result
 *   < <= > >=       int or float operands, bool result
 *   == !=           two numbers, or two bools; bool result
 *   && || !         bool operands, bool result
 *   unary -         int or float
 *   conditions      if, while and for conditions must be bool
 *
 *   Assigning a value (=, initializers, arguments, return) is allowed when
 *   the types are equal, or when an int goes into a float (widening, as in
 *   C). float -> int and anything involving bool otherwise is an error.
 *
 * Avoiding cascades: an expression with an error gets type TYPE_ERROR, and
 * any check involving TYPE_ERROR is skipped silently. One undeclared
 * variable therefore produces one message, not one per enclosing operator.
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "semantic.h"
#include "symtab.h"

int semantic_error_count = 0;

/* The function whose body is being checked (for return statements). */
static Symbol *current_function = NULL;

/* ================================================================
 * Helpers
 * ================================================================ */

static void sem_error(int line, int col, const char *fmt, ...)
{
    va_list args;

    fflush(stdout);   /* keep stdout/stderr in order */
    if (line > 0) {
        fprintf(stderr, "semantic error (line %d, col %d): ", line, col);
    } else {
        fprintf(stderr, "semantic error: ");
    }
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");

    semantic_error_count++;
}

static int is_numeric(Type t)
{
    return t == TYPE_INT || t == TYPE_FLOAT;
}

/* Can a value of type 'value' be stored where 'target' is expected? */
static int assignable(Type target, Type value)
{
    return target == value || (target == TYPE_FLOAT && value == TYPE_INT);
}

/* Reports a redeclaration if 'name' already exists in the current scope.
   Returns 1 if it does. */
static int check_redeclared(const char *name, int line, int col)
{
    Symbol *prev = symtab_lookup_current(name);
    if (prev != NULL) {
        sem_error(line, col, "'%s' is already declared in this scope (at line %d)",
                  name, prev->line);
        return 1;
    }
    return 0;
}

/* ================================================================
 * Expressions
 * ================================================================ */

static Type check_expr(Node *e);

static Type check_binary(Node *e)
{
    Type lt = check_expr(e->u.binary.left);
    Type rt = check_expr(e->u.binary.right);
    BinOp op = e->u.binary.op;

    if (lt == TYPE_ERROR || rt == TYPE_ERROR) {
        return TYPE_ERROR;     /* already reported */
    }

    switch (op) {
    case BIN_ADD: case BIN_SUB: case BIN_MUL: case BIN_DIV:
        if (is_numeric(lt) && is_numeric(rt)) {
            return (lt == TYPE_FLOAT || rt == TYPE_FLOAT) ? TYPE_FLOAT : TYPE_INT;
        }
        break;

    case BIN_MOD:
        if (lt == TYPE_INT && rt == TYPE_INT) {
            return TYPE_INT;
        }
        break;

    case BIN_LT: case BIN_LE: case BIN_GT: case BIN_GE:
        if (is_numeric(lt) && is_numeric(rt)) {
            return TYPE_BOOL;
        }
        break;

    case BIN_EQ: case BIN_NE:
        if ((is_numeric(lt) && is_numeric(rt))
            || (lt == TYPE_BOOL && rt == TYPE_BOOL)) {
            return TYPE_BOOL;
        }
        break;

    case BIN_AND: case BIN_OR:
        if (lt == TYPE_BOOL && rt == TYPE_BOOL) {
            return TYPE_BOOL;
        }
        break;

    case BIN_SHL:
        break;      /* never in the AST: only the optimizer creates it */
    }

    sem_error(e->line, e->col, "operator '%s' cannot be applied to %s and %s",
              binop_name(op), type_name(lt), type_name(rt));
    return TYPE_ERROR;
}

static Type check_unary(Node *e)
{
    Type t = check_expr(e->u.unary.operand);

    if (t == TYPE_ERROR) {
        return TYPE_ERROR;
    }
    if (e->u.unary.op == UN_NEG && is_numeric(t)) {
        return t;
    }
    if (e->u.unary.op == UN_NOT && t == TYPE_BOOL) {
        return TYPE_BOOL;
    }

    sem_error(e->line, e->col, "operator '%s' cannot be applied to %s",
              unop_name(e->u.unary.op), type_name(t));
    return TYPE_ERROR;
}

static Type check_var(Node *e)
{
    Symbol *sym = symtab_lookup(e->u.var.name);

    if (sym == NULL) {
        sem_error(e->line, e->col, "undeclared variable '%s'", e->u.var.name);
        return TYPE_ERROR;
    }
    if (sym->kind == SYM_FUNC) {
        sem_error(e->line, e->col, "'%s' is a function, not a variable "
                  "(missing parentheses?)", e->u.var.name);
        return TYPE_ERROR;
    }
    e->sym = sym;
    return sym->type;
}

static Type check_call(Node *e)
{
    const char *name = e->u.call.name;
    NodeList *args = e->u.call.args;
    Symbol *sym = symtab_lookup(name);

    /* Check the arguments first, so errors inside them are always found. */
    for (int i = 0; i < args->count; i++) {
        check_expr(args->items[i]);
    }

    if (sym == NULL) {
        sem_error(e->line, e->col, "undeclared function '%s'", name);
        return TYPE_ERROR;
    }
    if (sym->kind != SYM_FUNC) {
        sem_error(e->line, e->col, "'%s' is a %s, not a function", name,
                  sym->kind == SYM_PARAM ? "parameter" : "variable");
        return TYPE_ERROR;
    }
    e->sym = sym;

    if (args->count != sym->param_count) {
        sem_error(e->line, e->col,
                  "function '%s' expects %d argument%s, but %d %s given",
                  name, sym->param_count, sym->param_count == 1 ? "" : "s",
                  args->count, args->count == 1 ? "was" : "were");
        return sym->type;   /* the call's type is still known */
    }

    for (int i = 0; i < args->count; i++) {
        Node *arg = args->items[i];
        Type expected = sym->param_types[i];
        if (arg->type != TYPE_ERROR && !assignable(expected, arg->type)) {
            sem_error(arg->line, arg->col,
                      "argument %d of '%s' must be %s, but it is %s",
                      i + 1, name, type_name(expected), type_name(arg->type));
        }
    }
    return sym->type;
}

/* Checks an expression, stores its type in e->type and returns it. */
static Type check_expr(Node *e)
{
    Type t = TYPE_ERROR;

    switch (e->kind) {
    case N_INT_LIT:   t = TYPE_INT;   break;
    case N_FLOAT_LIT: t = TYPE_FLOAT; break;
    case N_BOOL_LIT:  t = TYPE_BOOL;  break;
    case N_VAR:       t = check_var(e);    break;
    case N_CALL:      t = check_call(e);   break;
    case N_BINARY:    t = check_binary(e); break;
    case N_UNARY:     t = check_unary(e);  break;
    default:
        break;   /* not an expression: cannot happen */
    }

    e->type = t;
    return t;
}

/* A condition of if/while/for must be bool. */
static void check_condition(Node *cond, const char *stmt_name)
{
    Type t = check_expr(cond);
    if (t != TYPE_ERROR && t != TYPE_BOOL) {
        sem_error(cond->line, cond->col,
                  "condition of '%s' must be bool, but it is %s",
                  stmt_name, type_name(t));
    }
}

/* Checks that a value of type 'value' can be stored in variable 'name'. */
static void check_assignable(Type target, Type value, const char *name,
                             int line, int col)
{
    if (value != TYPE_ERROR && !assignable(target, value)) {
        sem_error(line, col, "cannot assign a value of type %s to %s variable '%s'",
                  type_name(value), type_name(target), name);
    }
}

/* ================================================================
 * Statements
 * ================================================================ */

static void check_stmt(Node *s);

static void check_stmt_list(NodeList *stmts)
{
    for (int i = 0; i < stmts->count; i++) {
        check_stmt(stmts->items[i]);
    }
}

/* A local or global variable declaration. */
static void check_var_decl(Node *d)
{
    const char *name = d->u.var_decl.name;
    Type type = d->u.var_decl.type;

    /* The initializer is checked BEFORE the variable is declared, so
       "int x = x;" does not refer to the new x. */
    if (d->u.var_decl.init != NULL) {
        Type t = check_expr(d->u.var_decl.init);
        check_assignable(type, t, name, d->line, d->col);
    }

    if (!check_redeclared(name, d->line, d->col)) {
        d->sym = symtab_declare(name, SYM_VAR, type, d->line, d->col);
    }
}

static void check_assign(Node *s)
{
    const char *name = s->u.assign.name;
    Symbol *sym = symtab_lookup(name);
    Type value = check_expr(s->u.assign.value);

    if (sym == NULL) {
        sem_error(s->line, s->col, "undeclared variable '%s'", name);
        return;
    }
    if (sym->kind == SYM_FUNC) {
        sem_error(s->line, s->col, "cannot assign to function '%s'", name);
        return;
    }
    s->sym = sym;
    check_assignable(sym->type, value, name, s->line, s->col);
}

static void check_return(Node *s)
{
    Node *value = s->u.return_stmt.value;
    Type ret = current_function->type;
    const char *fname = current_function->name;

    if (value == NULL) {
        if (ret != TYPE_VOID) {
            sem_error(s->line, s->col,
                      "function '%s' must return a value of type %s",
                      fname, type_name(ret));
        }
        return;
    }

    Type t = check_expr(value);
    if (ret == TYPE_VOID) {
        sem_error(s->line, s->col,
                  "void function '%s' cannot return a value", fname);
    } else if (t != TYPE_ERROR && !assignable(ret, t)) {
        sem_error(s->line, s->col,
                  "function '%s' must return %s, but this returns %s",
                  fname, type_name(ret), type_name(t));
    }
}

static void check_stmt(Node *s)
{
    char scope_name[256];

    switch (s->kind) {
    case N_VAR_DECL:
        check_var_decl(s);
        break;

    case N_ASSIGN:
        check_assign(s);
        break;

    case N_BLOCK:
        /* A nested block gets its own scope, e.g. "main/block@12". */
        snprintf(scope_name, sizeof scope_name, "%s/block@%d",
                 scope_current_name(), s->line);
        scope_push(scope_name);
        check_stmt_list(s->u.block.stmts);
        scope_pop();
        break;

    case N_IF:
        check_condition(s->u.if_stmt.cond, "if");
        check_stmt(s->u.if_stmt.then_stmt);
        if (s->u.if_stmt.else_stmt != NULL) {
            check_stmt(s->u.if_stmt.else_stmt);
        }
        break;

    case N_WHILE:
        check_condition(s->u.while_stmt.cond, "while");
        check_stmt(s->u.while_stmt.body);
        break;

    case N_FOR:
        if (s->u.for_stmt.init != NULL)   check_stmt(s->u.for_stmt.init);
        if (s->u.for_stmt.cond != NULL)   check_condition(s->u.for_stmt.cond, "for");
        if (s->u.for_stmt.update != NULL) check_stmt(s->u.for_stmt.update);
        check_stmt(s->u.for_stmt.body);
        break;

    case N_RETURN:
        check_return(s);
        break;

    case N_PRINT: {
        Type t = check_expr(s->u.print_stmt.value);
        if (t == TYPE_VOID) {
            sem_error(s->line, s->col, "cannot print a void value");
        }
        break;
    }

    case N_EXPR_STMT:
        check_expr(s->u.expr_stmt.expr);
        break;

    default:
        break;
    }
}

/*
 * Does this statement always end in a return? Used to check that a
 * non-void function returns a value on every path. The check is simple
 * (conservative): it does not try to work out whether a loop always runs.
 */
static int always_returns(const Node *s)
{
    if (s == NULL) {
        return 0;
    }
    switch (s->kind) {
    case N_RETURN:
        return 1;
    case N_BLOCK:
        for (int i = 0; i < s->u.block.stmts->count; i++) {
            if (always_returns(s->u.block.stmts->items[i])) {
                return 1;
            }
        }
        return 0;
    case N_IF:
        return always_returns(s->u.if_stmt.then_stmt)
            && always_returns(s->u.if_stmt.else_stmt);
    default:
        return 0;
    }
}

/* ================================================================
 * Declarations
 * ================================================================ */

/* Pass 1: enter a function and its parameter types into the global scope. */
static void declare_function(Node *f)
{
    const char *name = f->u.func_decl.name;
    NodeList *params = f->u.func_decl.params;

    if (check_redeclared(name, f->line, f->col)) {
        return;
    }

    Symbol *sym = symtab_declare(name, SYM_FUNC, f->u.func_decl.ret_type,
                                 f->line, f->col);
    sym->param_count = params->count;
    sym->param_types = xmalloc((params->count + 1) * sizeof(Type));
    for (int i = 0; i < params->count; i++) {
        sym->param_types[i] = params->items[i]->u.param.type;
    }
    f->sym = sym;
}

/* Pass 2: check a function body. */
static void check_function(Node *f)
{
    if (f->sym == NULL) {
        return;     /* it was a redeclaration; already reported */
    }

    current_function = f->sym;
    symtab_begin_function();

    /* Parameters and the top-level locals of the body share one scope,
       so "int f(int a) { int a; }" is a redeclaration, as in C. */
    scope_push(f->u.func_decl.name);

    NodeList *params = f->u.func_decl.params;
    for (int i = 0; i < params->count; i++) {
        Node *p = params->items[i];
        if (!check_redeclared(p->u.param.name, p->line, p->col)) {
            p->sym = symtab_declare(p->u.param.name, SYM_PARAM,
                                    p->u.param.type, p->line, p->col);
        }
    }

    check_stmt_list(f->u.func_decl.body->u.block.stmts);

    if (f->sym->type != TYPE_VOID && !always_returns(f->u.func_decl.body)) {
        sem_error(f->line, f->col,
                  "function '%s' does not return a value on every path",
                  f->u.func_decl.name);
    }

    scope_pop();
    current_function = NULL;
}

/* Is this expression made only of literals and operators (no variables or
   calls)? Global initializers must be constant, as in C. */
static int is_constant_expr(const Node *e)
{
    switch (e->kind) {
    case N_INT_LIT: case N_FLOAT_LIT: case N_BOOL_LIT:
        return 1;
    case N_UNARY:
        return is_constant_expr(e->u.unary.operand);
    case N_BINARY:
        return is_constant_expr(e->u.binary.left)
            && is_constant_expr(e->u.binary.right);
    default:
        return 0;
    }
}

/* Pass 1: enter a global variable into the global scope. */
static void declare_global_var(Node *d)
{
    if (!check_redeclared(d->u.var_decl.name, d->line, d->col)) {
        d->sym = symtab_declare(d->u.var_decl.name, SYM_VAR,
                                d->u.var_decl.type, d->line, d->col);
    }
}

/* Pass 2: a global initializer must be a constant of a suitable type. */
static void check_global_init(Node *d)
{
    Node *init = d->u.var_decl.init;

    if (init == NULL) {
        return;
    }
    if (!is_constant_expr(init)) {
        sem_error(init->line, init->col,
                  "initializer of global variable '%s' must be a constant",
                  d->u.var_decl.name);
        return;
    }
    Type t = check_expr(init);
    check_assignable(d->u.var_decl.type, t, d->u.var_decl.name,
                     d->line, d->col);
}

void semantic_analyze(Node *program)
{
    NodeList *decls = program->u.program.decls;

    semantic_error_count = 0;
    scope_push("global");

    /* Pass 1: every global name, in source order. */
    for (int i = 0; i < decls->count; i++) {
        Node *d = decls->items[i];
        if (d->kind == N_VAR_DECL) {
            declare_global_var(d);
        } else {
            declare_function(d);
        }
    }

    /* Pass 2: global initializers and function bodies. */
    for (int i = 0; i < decls->count; i++) {
        Node *d = decls->items[i];
        if (d->kind == N_VAR_DECL) {
            check_global_init(d);
        } else {
            check_function(d);
        }
    }

    /* The program must have a main function without parameters. */
    Symbol *main_sym = symtab_lookup("main");
    if (main_sym == NULL || main_sym->kind != SYM_FUNC) {
        sem_error(0, 0, "the program has no 'main' function");
    } else if (main_sym->param_count != 0) {
        sem_error(main_sym->line, main_sym->col,
                  "'main' must not have parameters");
    }

    /* The global scope stays open, so --symbols can still look things up;
       symtab_free() closes it. */
}
