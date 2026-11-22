/*
 * irgen.c -- Phase 4: AST -> three-address code.
 *
 * The generator walks the AST recursively. For an expression it emits the
 * instructions that compute its value and returns an Operand holding the
 * result:
 *   - a constant or variable needs no instructions: the operand is returned
 *     directly (Int 5 -> 5, Var x -> x);
 *   - an operator gets a new temporary: "a + b * c" becomes
 *         t1 = b * c
 *         t2 = a + t1        (and t2 is returned)
 *
 * Statements are translated with labels and jumps, e.g.
 *
 *   while (cond) body          L1:
 *                                  t = cond
 *                                  ifFalse t goto L2
 *                                  body
 *                                  goto L1
 *                              L2:
 *
 * Implicit int -> float widening (allowed by Phase 3) is made explicit with
 * a conversion instruction: t = (float) x.
 *
 * && and || use short-circuit evaluation, as in C: the right operand is
 * only evaluated when it can change the result.
 */
#include <stdlib.h>
#include <string.h>

#include "irgen.h"
#include "symtab.h"

static TacFunc *cur;          /* function being generated           */
static Type     cur_ret_type; /* its return type (for conversions)  */

/* ================================================================
 * Emitting instructions
 * ================================================================ */

static Tac make(TacKind kind)
{
    Tac t;
    memset(&t, 0, sizeof t);
    t.kind = kind;
    t.dst = t.a = t.b = opd_none();
    return t;
}

/* 'type' is the type of the operands (see Tac.type). */
static void emit_binary(Operand dst, BinOp op, Operand a, Operand b, Type type)
{
    Tac t = make(TAC_BINARY);
    t.dst = dst; t.binop = op; t.a = a; t.b = b; t.type = type;
    tac_emit(cur, t);
}

static void emit_unary(Operand dst, TacUnOp op, Operand a, Type type)
{
    Tac t = make(TAC_UNARY);
    t.dst = dst; t.unop = op; t.a = a; t.type = type;
    tac_emit(cur, t);
}

static void emit_label(int label)
{
    Tac t = make(TAC_LABEL);
    t.label = label;
    tac_emit(cur, t);
}

static void emit_jump(TacKind kind, Operand cond, int label)
{
    Tac t = make(kind);          /* TAC_GOTO, TAC_IF_FALSE or TAC_IF_TRUE */
    t.a = cond; t.label = label;
    tac_emit(cur, t);
}

static void emit_simple(TacKind kind, Operand a)
{
    Tac t = make(kind);          /* TAC_PARAM, TAC_RETURN or TAC_PRINT */
    t.a = a;
    tac_emit(cur, t);
}

/*
 * Emits "dst = value". If value is a temporary that the previous
 * instruction just computed, that instruction is changed to write dst
 * directly. So "x = a + b" becomes one instruction (x = a + b) instead of
 * two (t1 = a + b; x = t1). This is safe because a temporary returned by
 * gen_expr() is used exactly once.
 */
static void assign_to(Operand dst, Operand value)
{
    if (value.kind == OPD_TEMP && cur->count > 0) {
        Tac *last = &cur->code[cur->count - 1];
        int writes_value = last->kind == TAC_BINARY || last->kind == TAC_UNARY
                        || last->kind == TAC_CALL   || last->kind == TAC_COPY;
        if (writes_value && opd_equal(last->dst, value)) {
            last->dst = dst;
            /* The temporary is no longer used anywhere. If it was the
               newest one, reuse its number, so the temporaries in the
               output are numbered t1, t2, t3, ... without gaps. */
            if (value.u.temp == cur->next_temp - 1) {
                cur->next_temp--;
            }
            return;
        }
    }
    Tac t = make(TAC_COPY);
    t.dst = dst; t.a = value;
    tac_emit(cur, t);
}

/* Converts an operand from one type to another if needed (int -> float). */
static Operand convert(Operand value, Type from, Type to)
{
    if (from == TYPE_INT && to == TYPE_FLOAT) {
        Operand t = tac_new_temp(cur);
        emit_unary(t, TAC_ITOF, value, TYPE_INT);
        return t;
    }
    return value;
}

static Operand var_operand(const Symbol *sym)
{
    return opd_var(sym->ir_name);
}

/* ================================================================
 * Expressions
 * ================================================================ */

static Operand gen_expr(Node *e);

/* Generates an expression and converts its value to 'target'. */
static Operand gen_expr_as(Node *e, Type target)
{
    return convert(gen_expr(e), e->type, target);
}

/*
 * a && b:                       a || b:
 *     t = a                         t = a
 *     ifFalse t goto L              if t goto L
 *     t = b                         t = b
 *   L:                            L:
 */
static Operand gen_short_circuit(Node *e)
{
    Operand t = tac_new_temp(cur);
    int done = tac_new_label(cur);

    assign_to(t, gen_expr(e->u.binary.left));
    emit_jump(e->u.binary.op == BIN_AND ? TAC_IF_FALSE : TAC_IF_TRUE, t, done);
    assign_to(t, gen_expr(e->u.binary.right));
    emit_label(done);
    return t;
}

static Operand gen_binary(Node *e)
{
    Node *left = e->u.binary.left;
    Node *right = e->u.binary.right;
    BinOp op = e->u.binary.op;

    if (op == BIN_AND || op == BIN_OR) {
        return gen_short_circuit(e);
    }

    /* Both operands must have the same type. If one is float and the
       other int, the int one is converted (e.g. i < 2.5). */
    Type operand_type = left->type;
    if (left->type == TYPE_FLOAT || right->type == TYPE_FLOAT) {
        operand_type = TYPE_FLOAT;
    }

    Operand a = gen_expr_as(left, operand_type);
    Operand b = gen_expr_as(right, operand_type);
    Operand t = tac_new_temp(cur);
    emit_binary(t, op, a, b, operand_type);
    return t;
}

/*
 * f(a, b) evaluates every argument first, then passes them:
 *     ...code for a and b...
 *     param a
 *     param b
 *     t = call f, 2
 * With want_result == 0 (a call used as a statement), no result is kept.
 */
static Operand gen_call(Node *e, int want_result)
{
    const Symbol *f = e->sym;
    NodeList *args = e->u.call.args;
    Operand *values = xmalloc((args->count + 1) * sizeof *values);

    for (int i = 0; i < args->count; i++) {
        values[i] = gen_expr_as(args->items[i], f->param_types[i]);
    }
    for (int i = 0; i < args->count; i++) {
        emit_simple(TAC_PARAM, values[i]);
    }
    free(values);

    Tac call = make(TAC_CALL);
    call.func = f->ir_name;
    call.nargs = args->count;
    if (want_result && f->type != TYPE_VOID) {
        call.dst = tac_new_temp(cur);
    }
    tac_emit(cur, call);
    return call.dst;
}

static Operand gen_expr(Node *e)
{
    switch (e->kind) {
    case N_INT_LIT:   return opd_int(e->u.int_val);
    case N_FLOAT_LIT: return opd_float(e->u.float_val);
    case N_BOOL_LIT:  return opd_bool(e->u.bool_val);
    case N_VAR:       return var_operand(e->sym);
    case N_CALL:      return gen_call(e, 1);
    case N_BINARY:    return gen_binary(e);

    case N_UNARY: {
        Operand a = gen_expr(e->u.unary.operand);
        Operand t = tac_new_temp(cur);
        emit_unary(t, e->u.unary.op == UN_NEG ? TAC_NEG : TAC_NOT, a,
                   e->u.unary.operand->type);
        return t;
    }

    default:
        return opd_none();   /* not an expression: cannot happen */
    }
}

/* ================================================================
 * Statements
 * ================================================================ */

static void gen_stmt(Node *s);

static void gen_stmt_list(NodeList *stmts)
{
    for (int i = 0; i < stmts->count; i++) {
        gen_stmt(stmts->items[i]);
    }
}

/*
 * if (c) A            t = c                 if (c) A else B     t = c
 *                     ifFalse t goto L1                         ifFalse t goto L1
 *                     A                                         A
 *                   L1:                                         goto L2
 *                                                             L1:
 *                                                               B
 *                                                             L2:
 */
static void gen_if(Node *s)
{
    Operand cond = gen_expr(s->u.if_stmt.cond);
    int else_label = tac_new_label(cur);

    emit_jump(TAC_IF_FALSE, cond, else_label);
    gen_stmt(s->u.if_stmt.then_stmt);

    if (s->u.if_stmt.else_stmt == NULL) {
        emit_label(else_label);
    } else {
        int end_label = tac_new_label(cur);
        emit_jump(TAC_GOTO, opd_none(), end_label);
        emit_label(else_label);
        gen_stmt(s->u.if_stmt.else_stmt);
        emit_label(end_label);
    }
}

/*
 * for (init; cond; update) body       init
 *                                   L1:
 *                                     t = cond
 *                                     ifFalse t goto L2
 *                                     body
 *                                     update
 *                                     goto L1
 *                                   L2:
 * A while loop is the same without init and update.
 * With no condition (for (;;)), the test is left out.
 */
static void gen_loop(Node *init, Node *cond, Node *update, Node *body)
{
    if (init != NULL) {
        gen_stmt(init);
    }

    int start = tac_new_label(cur);
    int end = tac_new_label(cur);

    emit_label(start);
    if (cond != NULL) {
        emit_jump(TAC_IF_FALSE, gen_expr(cond), end);
    }
    gen_stmt(body);
    if (update != NULL) {
        gen_stmt(update);
    }
    emit_jump(TAC_GOTO, opd_none(), start);
    emit_label(end);
}

static void gen_stmt(Node *s)
{
    switch (s->kind) {
    case N_VAR_DECL:
        /* A local without an initializer generates no code. */
        if (s->u.var_decl.init != NULL) {
            assign_to(var_operand(s->sym),
                      gen_expr_as(s->u.var_decl.init, s->u.var_decl.type));
        }
        break;

    case N_ASSIGN:
        assign_to(var_operand(s->sym),
                  gen_expr_as(s->u.assign.value, s->sym->type));
        break;

    case N_BLOCK:
        gen_stmt_list(s->u.block.stmts);
        break;

    case N_IF:
        gen_if(s);
        break;

    case N_WHILE:
        gen_loop(NULL, s->u.while_stmt.cond, NULL, s->u.while_stmt.body);
        break;

    case N_FOR:
        gen_loop(s->u.for_stmt.init, s->u.for_stmt.cond,
                 s->u.for_stmt.update, s->u.for_stmt.body);
        break;

    case N_RETURN:
        if (s->u.return_stmt.value != NULL) {
            emit_simple(TAC_RETURN, gen_expr_as(s->u.return_stmt.value, cur_ret_type));
        } else {
            emit_simple(TAC_RETURN, opd_none());
        }
        break;

    case N_PRINT:
        emit_simple(TAC_PRINT, gen_expr(s->u.print_stmt.value));
        break;

    case N_EXPR_STMT:
        if (s->u.expr_stmt.expr->kind == N_CALL) {
            gen_call(s->u.expr_stmt.expr, 0);   /* result not needed */
        } else {
            gen_expr(s->u.expr_stmt.expr);
        }
        break;

    default:
        break;
    }
}

/* ================================================================
 * Functions and the program
 * ================================================================ */

static TacFunc *gen_function(Node *fn)
{
    const Symbol *sym = fn->sym;
    NodeList *params = fn->u.func_decl.params;
    TacFunc *f = tac_func_new(sym->ir_name);

    f->param_count = params->count;
    f->params = xmalloc((params->count + 1) * sizeof *f->params);
    for (int i = 0; i < params->count; i++) {
        f->params[i] = params->items[i]->sym->ir_name;
    }

    cur = f;
    cur_ret_type = sym->type;
    gen_stmt_list(fn->u.func_decl.body->u.block.stmts);

    /* A void function may end without "return"; add one. (Phase 3 made
       sure that other functions return on every path.) */
    if (sym->type == TYPE_VOID
        && (f->count == 0 || f->code[f->count - 1].kind != TAC_RETURN)) {
        emit_simple(TAC_RETURN, opd_none());
    }
    return f;
}

/* The value a global variable starts with when it has no initializer. */
static Operand zero_value(Type t)
{
    switch (t) {
    case TYPE_FLOAT: return opd_float(0.0);
    case TYPE_BOOL:  return opd_bool(0);
    default:         return opd_int(0);
    }
}

TacProgram *irgen_program(Node *program)
{
    NodeList *decls = program->u.program.decls;
    TacProgram *p = xmalloc(sizeof *p);

    memset(p, 0, sizeof *p);
    p->global_names = xmalloc((decls->count + 1) * sizeof *p->global_names);
    p->global_types = xmalloc((decls->count + 1) * sizeof *p->global_types);
    p->funcs = xmalloc((decls->count + 1) * sizeof *p->funcs);
    p->init = tac_func_new("init");

    for (int i = 0; i < decls->count; i++) {
        Node *d = decls->items[i];

        if (d->kind == N_VAR_DECL) {
            /* Global variable: record it, and initialize it in "init".
               Like C, a global without an initializer starts at zero. */
            Type type = d->u.var_decl.type;
            p->global_names[p->global_count] = d->sym->ir_name;
            p->global_types[p->global_count] = type;
            p->global_count++;

            cur = p->init;
            if (d->u.var_decl.init != NULL) {
                assign_to(var_operand(d->sym), gen_expr_as(d->u.var_decl.init, type));
            } else {
                assign_to(var_operand(d->sym), zero_value(type));
            }
        } else {
            p->funcs[p->func_count++] = gen_function(d);
        }
    }
    return p;
}
