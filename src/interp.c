/*
 * interp.c -- An interpreter for three-address code. See interp.h.
 *
 * Each function call gets a Frame holding its local variables and
 * temporaries. Global variables live in one shared frame. Because the code
 * generator gives every local a name that differs from all globals (x_1,
 * ...), a name is global exactly when it is in the program's global list.
 *
 * Values carry their kind (int, float or bool). Integer arithmetic wraps
 * around on overflow, the same way the optimizer's constant folding does.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interp.h"

typedef enum { V_INT, V_FLOAT, V_BOOL } ValueKind;

typedef struct {
    ValueKind kind;
    int       i;     /* V_INT, V_BOOL */
    double    f;     /* V_FLOAT       */
} Value;

typedef struct {
    const char *name;
    Value       value;
} Slot;

typedef struct {
    Slot  *vars;
    int    var_count;
    int    var_capacity;
    Value *temps;        /* temps[n] holds tn */
    int    temp_count;
} Frame;

/* Limits, so that a broken program cannot hang or crash the compiler. */
enum {
    MAX_STEPS = 10000000,
    MAX_DEPTH = 1000,
    MAX_ARGS  = 4096
};

static const TacProgram *prog;
static Frame  globals;
static Value  arg_stack[MAX_ARGS];   /* values passed with 'param' */
static int    arg_top;
static long   steps;
static long   executed;              /* instructions run, not counting labels */
static int    depth;
static int    failed;                /* set on a runtime error */

static void runtime_error(const char *msg)
{
    if (!failed) {
        fflush(stdout);
        fprintf(stderr, "runtime error: %s\n", msg);
        failed = 1;
    }
}

/* ================================================================
 * Variables
 * ================================================================ */

static int is_global(const char *name)
{
    for (int i = 0; i < prog->global_count; i++) {
        if (strcmp(prog->global_names[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Finds a variable's slot in a frame, creating it (as int 0) if needed. */
static Value *slot(Frame *fr, const char *name)
{
    for (int i = 0; i < fr->var_count; i++) {
        if (strcmp(fr->vars[i].name, name) == 0) {
            return &fr->vars[i].value;
        }
    }
    if (fr->var_count == fr->var_capacity) {
        fr->var_capacity = fr->var_capacity == 0 ? 8 : fr->var_capacity * 2;
        fr->vars = realloc(fr->vars, fr->var_capacity * sizeof *fr->vars);
        if (fr->vars == NULL) {
            fprintf(stderr, "fatal: out of memory\n");
            exit(2);
        }
    }
    Slot *s = &fr->vars[fr->var_count++];
    s->name = name;
    s->value.kind = V_INT;
    s->value.i = 0;
    s->value.f = 0.0;
    return &s->value;
}

static Value *var_slot(Frame *fr, const char *name)
{
    return slot(is_global(name) ? &globals : fr, name);
}

static Value read(Frame *fr, Operand o)
{
    Value v = { V_INT, 0, 0.0 };

    switch (o.kind) {
    case OPD_INT:   v.kind = V_INT;   v.i = o.u.ival; break;
    case OPD_BOOL:  v.kind = V_BOOL;  v.i = o.u.ival; break;
    case OPD_FLOAT: v.kind = V_FLOAT; v.f = o.u.fval; break;
    case OPD_VAR:   v = *var_slot(fr, o.u.name); break;
    case OPD_TEMP:  v = fr->temps[o.u.temp]; break;
    case OPD_NONE:  break;
    }
    return v;
}

static void write(Frame *fr, Operand o, Value v)
{
    if (o.kind == OPD_VAR) {
        *var_slot(fr, o.u.name) = v;
    } else if (o.kind == OPD_TEMP) {
        fr->temps[o.u.temp] = v;
    }
}

/* ================================================================
 * Operators
 * ================================================================ */

static Value make_int(int i)     { Value v = { V_INT, i, 0.0 };    return v; }
static Value make_bool(int b)    { Value v = { V_BOOL, !!b, 0.0 }; return v; }
static Value make_float(double f){ Value v = { V_FLOAT, 0, f };    return v; }

static Value binary(BinOp op, Value a, Value b)
{
    if (a.kind == V_FLOAT || b.kind == V_FLOAT) {
        double x = a.kind == V_FLOAT ? a.f : a.i;
        double y = b.kind == V_FLOAT ? b.f : b.i;
        switch (op) {
        case BIN_ADD: return make_float(x + y);
        case BIN_SUB: return make_float(x - y);
        case BIN_MUL: return make_float(x * y);
        case BIN_DIV: return make_float(x / y);
        case BIN_EQ:  return make_bool(x == y);
        case BIN_NE:  return make_bool(x != y);
        case BIN_LT:  return make_bool(x < y);
        case BIN_LE:  return make_bool(x <= y);
        case BIN_GT:  return make_bool(x > y);
        case BIN_GE:  return make_bool(x >= y);
        default: break;
        }
        runtime_error("invalid float operation");
        return make_float(0.0);
    }

    /* int (or bool for == and !=). Wrap-around arithmetic via unsigned. */
    unsigned ux = (unsigned)a.i, uy = (unsigned)b.i;
    switch (op) {
    case BIN_ADD: return make_int((int)(ux + uy));
    case BIN_SUB: return make_int((int)(ux - uy));
    case BIN_MUL: return make_int((int)(ux * uy));
    case BIN_SHL: return make_int((int)(ux << (uy & 31)));
    case BIN_DIV:
    case BIN_MOD:
        if (b.i == 0) {
            runtime_error("division by zero");
            return make_int(0);
        }
        if (a.i == -2147483647 - 1 && b.i == -1) {
            return make_int(op == BIN_DIV ? a.i : 0);   /* avoid overflow trap */
        }
        return make_int(op == BIN_DIV ? a.i / b.i : a.i % b.i);
    case BIN_EQ:  return make_bool(a.i == b.i);
    case BIN_NE:  return make_bool(a.i != b.i);
    case BIN_LT:  return make_bool(a.i < b.i);
    case BIN_LE:  return make_bool(a.i <= b.i);
    case BIN_GT:  return make_bool(a.i > b.i);
    case BIN_GE:  return make_bool(a.i >= b.i);
    default: break;
    }
    runtime_error("invalid int operation");
    return make_int(0);
}

static Value unary(TacUnOp op, Value a)
{
    switch (op) {
    case TAC_NEG:
        return a.kind == V_FLOAT ? make_float(-a.f) : make_int((int)(0u - (unsigned)a.i));
    case TAC_NOT:
        return make_bool(!a.i);
    case TAC_ITOF:
        return make_float((double)a.i);
    }
    return a;
}

static void print_value(Value v)
{
    char buf[64];

    switch (v.kind) {
    case V_INT:   printf("%d\n", v.i); break;
    case V_BOOL:  printf("%s\n", v.i ? "true" : "false"); break;
    case V_FLOAT:
        format_float(v.f, buf, sizeof buf);
        printf("%s\n", buf);
        break;
    }
}

/* ================================================================
 * Executing functions
 * ================================================================ */

static const TacFunc *find_function(const char *name)
{
    for (int i = 0; i < prog->func_count; i++) {
        if (strcmp(prog->funcs[i]->name, name) == 0) {
            return prog->funcs[i];
        }
    }
    return NULL;
}

/* Runs f with the given arguments; stores its return value in *ret. */
static void exec_function(const TacFunc *f, const Value *args, Value *ret)
{
    Frame fr;
    memset(&fr, 0, sizeof fr);
    fr.temp_count = f->next_temp + 1;
    fr.temps = calloc(fr.temp_count, sizeof *fr.temps);

    /* Where each label is: label_at[n] = index of "Ln:" */
    int *label_at = calloc(f->next_label + 1, sizeof *label_at);
    for (int i = 0; i < f->count; i++) {
        if (f->code[i].kind == TAC_LABEL) {
            label_at[f->code[i].label] = i;
        }
    }

    for (int i = 0; i < f->param_count; i++) {
        *slot(&fr, f->params[i]) = args[i];
    }

    if (++depth > MAX_DEPTH) {
        runtime_error("too many nested calls (infinite recursion?)");
    }

    int pc = 0;
    while (pc < f->count && !failed) {
        const Tac *t = &f->code[pc];
        pc++;

        if (++steps > MAX_STEPS) {
            runtime_error("too many steps (infinite loop?)");
            break;
        }
        if (t->kind != TAC_LABEL) {
            executed++;
        }

        switch (t->kind) {
        case TAC_COPY:   write(&fr, t->dst, read(&fr, t->a)); break;
        case TAC_BINARY: write(&fr, t->dst, binary(t->binop, read(&fr, t->a), read(&fr, t->b))); break;
        case TAC_UNARY:  write(&fr, t->dst, unary(t->unop, read(&fr, t->a))); break;
        case TAC_LABEL:  break;
        case TAC_GOTO:   pc = label_at[t->label]; break;
        case TAC_IF_FALSE:
            if (!read(&fr, t->a).i) pc = label_at[t->label];
            break;
        case TAC_IF_TRUE:
            if (read(&fr, t->a).i) pc = label_at[t->label];
            break;
        case TAC_PARAM:
            if (arg_top == MAX_ARGS) {
                runtime_error("too many arguments");
                break;
            }
            arg_stack[arg_top++] = read(&fr, t->a);
            break;
        case TAC_CALL: {
            const TacFunc *callee = find_function(t->func);
            Value result = make_int(0);
            arg_top -= t->nargs;
            exec_function(callee, &arg_stack[arg_top], &result);
            write(&fr, t->dst, result);
            break;
        }
        case TAC_RETURN:
            if (t->a.kind != OPD_NONE) {
                *ret = read(&fr, t->a);
            }
            pc = f->count;
            break;
        case TAC_PRINT:
            print_value(read(&fr, t->a));
            break;
        }
    }

    depth--;
    free(label_at);
    free(fr.temps);
    free(fr.vars);
}

int interp_run(const TacProgram *p)
{
    Value ignored = make_int(0);

    prog = p;
    memset(&globals, 0, sizeof globals);
    arg_top = 0;
    steps = 0;
    executed = 0;
    depth = 0;
    failed = 0;

    exec_function(p->init, NULL, &ignored);

    const TacFunc *main_fn = find_function("main");
    if (main_fn != NULL && !failed) {
        exec_function(main_fn, NULL, &ignored);
    }

    /* How much work the program did. Unlike the instruction count of the
       code, this shows the effect of moving code out of loops or
       unrolling them. */
    printf("(%ld instruction%s executed)\n", executed, executed == 1 ? "" : "s");

    fflush(stdout);
    free(globals.vars);
    return failed;
}
