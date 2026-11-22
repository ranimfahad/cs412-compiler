/*
 * optimize.c -- Phase 5: optimizations on the three-address code.
 *
 * Every pass works on one function's instruction array at a time (the
 * "init" code counts as a function too). Passes change instructions in
 * place, remove them, or insert them.
 *
 * A few facts about the generated code that the passes rely on:
 *   - Temporaries hold the value of one expression and are used only inside
 *     the function. Most are written exactly once. The exception is the
 *     temporaries of && and ||, which are written twice, and the passes
 *     check for that where it matters.
 *   - A local variable never has the same name as a global (Phase 3 renames
 *     it). So a name is global exactly when it is in the global list.
 *   - A function call can change any global variable, but no local.
 *   - Loops look like this, with a backward "goto" to the loop's label:
 *
 *           i = 0                  (for loops: the init)
 *         L1:
 *           t1 = i < 10            (the condition)
 *           ifFalse t1 goto L2
 *           ...body...
 *           i = i + 1              (for loops: the update)
 *           goto L1
 *         L2:
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "optimize.h"

static const TacProgram *prog;   /* the program being optimized */

/* ================================================================
 * Helpers for inspecting and editing instructions
 * ================================================================ */

/* Does the instruction write a result? */
static int writes(const Tac *t)
{
    return (t->kind == TAC_COPY || t->kind == TAC_BINARY
            || t->kind == TAC_UNARY || t->kind == TAC_CALL)
           && t->dst.kind != OPD_NONE;
}

/* Is the instruction a computation without side effects? */
static int is_pure(const Tac *t)
{
    return t->kind == TAC_COPY || t->kind == TAC_BINARY || t->kind == TAC_UNARY;
}

static int is_jump(const Tac *t)
{
    return t->kind == TAC_GOTO || t->kind == TAC_IF_FALSE || t->kind == TAC_IF_TRUE;
}

/* Stores pointers to the operands an instruction READS into ops[].
   Returns how many there are (0, 1 or 2). */
static int read_operands(Tac *t, Operand *ops[2])
{
    int n = 0;
    switch (t->kind) {
    case TAC_BINARY:
        ops[n++] = &t->a;
        ops[n++] = &t->b;
        break;
    case TAC_COPY: case TAC_UNARY: case TAC_IF_FALSE: case TAC_IF_TRUE:
    case TAC_PARAM: case TAC_RETURN: case TAC_PRINT:
        if (t->a.kind != OPD_NONE) {
            ops[n++] = &t->a;
        }
        break;
    default:
        break;
    }
    return n;
}

static int reads(const Tac *t, Operand o)
{
    Operand *ops[2];
    int n = read_operands((Tac *)t, ops);
    for (int i = 0; i < n; i++) {
        if (opd_equal(*ops[i], o)) {
            return 1;
        }
    }
    return 0;
}

static int is_global(Operand o)
{
    if (o.kind != OPD_VAR) {
        return 0;
    }
    for (int i = 0; i < prog->global_count; i++) {
        if (strcmp(prog->global_names[i], o.u.name) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Is the operand read anywhere in the function? */
static int read_anywhere(const TacFunc *f, Operand o)
{
    for (int i = 0; i < f->count; i++) {
        if (reads(&f->code[i], o)) {
            return 1;
        }
    }
    return 0;
}

/* Is the operand written anywhere in code[from..to]? */
static int written_in(const TacFunc *f, int from, int to, Operand o)
{
    for (int i = from; i <= to; i++) {
        if (writes(&f->code[i]) && opd_equal(f->code[i].dst, o)) {
            return 1;
        }
    }
    return 0;
}

static int count_writes(const TacFunc *f, Operand o)
{
    int n = 0;
    for (int i = 0; i < f->count; i++) {
        if (writes(&f->code[i]) && opd_equal(f->code[i].dst, o)) {
            n++;
        }
    }
    return n;
}

static void remove_at(TacFunc *f, int i)
{
    memmove(&f->code[i], &f->code[i + 1], (f->count - i - 1) * sizeof *f->code);
    f->count--;
}

static void insert_at(TacFunc *f, int i, Tac t)
{
    tac_emit(f, t);   /* grows the array by one */
    memmove(&f->code[i + 1], &f->code[i], (f->count - 1 - i) * sizeof *f->code);
    f->code[i] = t;
}

/* Index of "Ln:", or -1. */
static int find_label(const TacFunc *f, int label)
{
    for (int i = 0; i < f->count; i++) {
        if (f->code[i].kind == TAC_LABEL && f->code[i].label == label) {
            return i;
        }
    }
    return -1;
}

/* Number of jumps to label n. */
static int label_refs(const TacFunc *f, int label)
{
    int n = 0;
    for (int i = 0; i < f->count; i++) {
        if (is_jump(&f->code[i]) && f->code[i].label == label) {
            n++;
        }
    }
    return n;
}

static void make_copy(Tac *t, Operand value)
{
    t->kind = TAC_COPY;
    t->a = value;
    t->b = opd_none();
}

/* ================================================================
 * Finding loops
 *
 * A loop is a backward jump: "goto L" where "L:" comes earlier. The loop
 * consists of the instructions from the label (head) to the goto (back).
 * ================================================================ */

typedef struct {
    int head;   /* index of the loop's label   */
    int back;   /* index of the backward goto  */
} Loop;

enum { MAX_LOOPS = 256 };

/* Finds all loops, sorted from smallest to largest, so inner loops come
   before the loops that contain them. */
static int find_loops(const TacFunc *f, Loop loops[MAX_LOOPS])
{
    int n = 0;

    for (int g = 0; g < f->count && n < MAX_LOOPS; g++) {
        if (f->code[g].kind == TAC_GOTO) {
            int s = find_label(f, f->code[g].label);
            if (s >= 0 && s < g) {
                loops[n].head = s;
                loops[n].back = g;
                n++;
            }
        }
    }

    /* Insertion sort by size. */
    for (int i = 1; i < n; i++) {
        Loop key = loops[i];
        int j = i - 1;
        while (j >= 0 && loops[j].back - loops[j].head > key.back - key.head) {
            loops[j + 1] = loops[j];
            j--;
        }
        loops[j + 1] = key;
    }
    return n;
}

/* ================================================================
 * Pass 1: loop unrolling
 *
 * A for loop whose number of iterations is a known constant is unrolled:
 *
 *   for (i = 0; i < 3; i = i + 1)        i = 0
 *       sum = sum + i;                   sum = sum + i
 *                                        i = i + 1
 *                                        sum = sum + i     (3 copies,
 *                                        i = i + 1          no tests,
 *                                        sum = sum + i      no jumps)
 *                                        i = i + 1
 *
 * - Full unrolling: when the copies stay small (at most UNROLL_MAX_TRIPS
 *   iterations and UNROLL_MAX_SIZE instructions), the loop disappears.
 * - Partial unrolling: otherwise, if the trip count is divisible by 4 (or
 *   2), the body is repeated 4 (or 2) times inside the loop, so the test and
 *   the jump run 4 (or 2) times less often.
 *
 * Requirements: the loop has the generated shape (i = c0 before the label,
 * test "i <rel> c1", update "i = i +/- c2"), i is a local variable, and the
 * body does not change i. The trip count is found by simulating i.
 * ================================================================ */

enum {
    UNROLL_MAX_TRIPS = 16,
    UNROLL_MAX_SIZE  = 64,
    TRIP_LIMIT       = 100000
};

static int compare(BinOp op, long long x, long long y)
{
    switch (op) {
    case BIN_LT: return x < y;
    case BIN_LE: return x <= y;
    case BIN_GT: return x > y;
    case BIN_GE: return x >= y;
    case BIN_NE: return x != y;
    case BIN_EQ: return x == y;
    default:     return 0;
    }
}

/* Number of iterations of: for (v = init; v rel bound; v = v +/- step).
   Returns -1 if it is very large or the variable would overflow. */
static int trip_count(int init, BinOp rel, int bound, BinOp step_op, int step)
{
    long long v = init;
    int n = 0;

    while (compare(rel, v, bound)) {
        if (++n > TRIP_LIMIT) {
            return -1;
        }
        v = step_op == BIN_ADD ? v + step : v - step;
        if (v > 2147483647LL || v < -2147483648LL) {
            return -1;
        }
    }
    return n;
}

/* Checks that the loop has the for-loop shape. On success returns 1 and
   sets *trips. */
static int match_counted_loop(const TacFunc *f, Loop L, int *trips)
{
    int s = L.head, g = L.back;

    if (s < 1 || s + 3 > g - 1 || g + 1 >= f->count) {
        return 0;
    }

    const Tac *init   = &f->code[s - 1];
    const Tac *cond   = &f->code[s + 1];
    const Tac *test   = &f->code[s + 2];
    const Tac *update = &f->code[g - 1];
    const Tac *end    = &f->code[g + 1];

    /* i = c0 */
    if (init->kind != TAC_COPY || init->dst.kind != OPD_VAR
        || init->a.kind != OPD_INT || is_global(init->dst)) {
        return 0;
    }
    Operand iv = init->dst;

    /* t = i <rel> c1 ; ifFalse t goto Lend */
    if (cond->kind != TAC_BINARY || cond->dst.kind != OPD_TEMP
        || !opd_equal(cond->a, iv) || cond->b.kind != OPD_INT
        || cond->binop < BIN_EQ || cond->binop > BIN_GE) {
        return 0;
    }
    if (test->kind != TAC_IF_FALSE || !opd_equal(test->a, cond->dst)) {
        return 0;
    }

    /* i = i +/- c2 ; goto Lstart ; Lend: */
    if (update->kind != TAC_BINARY || !opd_equal(update->dst, iv)
        || !opd_equal(update->a, iv) || update->b.kind != OPD_INT
        || (update->binop != BIN_ADD && update->binop != BIN_SUB)) {
        return 0;
    }
    if (end->kind != TAC_LABEL || end->label != test->label) {
        return 0;
    }

    /* Only the loop itself may jump to its two labels. */
    if (label_refs(f, f->code[s].label) != 1 || label_refs(f, test->label) != 1) {
        return 0;
    }

    /* The body must not change i, and its jumps must stay inside it. */
    for (int i = s + 3; i <= g - 2; i++) {
        const Tac *t = &f->code[i];
        if (writes(t) && opd_equal(t->dst, iv)) {
            return 0;
        }
        if (reads(t, cond->dst)) {
            return 0;
        }
        if (is_jump(t)) {
            int target = find_label(f, t->label);
            if (target < s + 3 || target > g - 2) {
                return 0;
            }
        }
    }

    *trips = trip_count(init->a.u.ival, cond->binop, cond->b.u.ival,
                        update->binop, update->b.u.ival);
    return *trips >= 0;
}

/* Appends 'copies' copies of code[from..to] to out. Labels defined in that
   range get new numbers in each copy, so the copies don't share labels. */
static void append_copies(TacFunc *f, TacFunc *out, int from, int to, int copies)
{
    int old_labels[64], new_labels[64];
    int nlabels;

    for (int c = 0; c < copies; c++) {
        nlabels = 0;
        for (int i = from; i <= to; i++) {
            if (f->code[i].kind == TAC_LABEL && nlabels < 64) {
                old_labels[nlabels] = f->code[i].label;
                new_labels[nlabels] = tac_new_label(f);
                nlabels++;
            }
        }
        for (int i = from; i <= to; i++) {
            Tac t = f->code[i];
            if (t.kind == TAC_LABEL || is_jump(&t)) {
                for (int k = 0; k < nlabels; k++) {
                    if (t.label == old_labels[k]) {
                        t.label = new_labels[k];
                        break;
                    }
                }
            }
            tac_emit(out, t);
        }
    }
}

/* Replaces code[from..to] with the instructions in 'repl'. */
static void replace_range(TacFunc *f, int from, int to, TacFunc *repl)
{
    TacFunc *out = tac_func_new(f->name);

    for (int i = 0; i < from; i++)        tac_emit(out, f->code[i]);
    for (int i = 0; i < repl->count; i++) tac_emit(out, repl->code[i]);
    for (int i = to + 1; i < f->count; i++) tac_emit(out, f->code[i]);

    free(f->code);
    f->code = out->code;
    f->count = out->count;
    f->capacity = out->capacity;
    out->code = NULL;
    tac_func_free(out);
}

static void unroll_function(TacFunc *f, int *full, int *partial, int *iterations)
{
    Loop loops[MAX_LOOPS];
    int changed = 1;

    while (changed) {
        changed = 0;
        int n = find_loops(f, loops);

        for (int k = 0; k < n && !changed; k++) {
            int s = loops[k].head, g = loops[k].back, trips;
            if (!match_counted_loop(f, loops[k], &trips)) {
                continue;
            }

            int per_copy = (g - 1) - (s + 3) + 1;   /* body + update */
            TacFunc *copies = tac_func_new(f->name);

            if (trips <= UNROLL_MAX_TRIPS && trips * per_copy <= UNROLL_MAX_SIZE) {
                /* Full: label, test, body, update, goto and end label are
                   all replaced by 'trips' copies of body + update. */
                append_copies(f, copies, s + 3, g - 1, trips);
                replace_range(f, s, g + 1, copies);
                (*full)++;
                *iterations += trips;
                changed = 1;
            } else {
                for (int factor = 4; factor >= 2; factor /= 2) {
                    if (trips % factor == 0 && trips >= factor
                        && factor * per_copy <= UNROLL_MAX_SIZE) {
                        /* Partial: keep the test and the jump, repeat
                           body + update 'factor' times. */
                        append_copies(f, copies, s + 3, g - 1, factor);
                        replace_range(f, s + 3, g - 1, copies);
                        (*partial)++;
                        changed = 1;
                        break;
                    }
                }
            }
            copies->count = 0;
            tac_func_free(copies);
        }
    }
}

/* ================================================================
 * Pass 2: constant folding (with constant propagation)
 *
 * Folding: an operation whose operands are all constants is computed at
 * compile time:  t1 = 2 * 3   ->   t1 = 6
 *
 * Propagation: after "x = 6", later uses of x in the same basic block are
 * replaced by 6, which often creates more folding opportunities. Copies
 * are propagated the same way: after "c = x", a use of c becomes a use of
 * x (until c or x is changed), and "c = x" may then become dead.
 * Everything known is forgotten at every label (code can be reached from
 * elsewhere there), and anything involving a global after a call.
 *
 * Branches: "ifFalse true goto L" never jumps and is removed, and
 * "ifFalse false goto L" always jumps and becomes "goto L". The code that
 * can no longer be reached is removed later by dead code elimination.
 *
 * Not folded: integer division by zero (it must fail at run time), and
 * float division by zero.
 * ================================================================ */

enum { MAX_KNOWN = 512 };

static Operand known_name[MAX_KNOWN];
static Operand known_value[MAX_KNOWN];
static int     known_count;

static int known_find(Operand o)
{
    for (int i = 0; i < known_count; i++) {
        if (opd_equal(known_name[i], o)) {
            return i;
        }
    }
    return -1;
}

static void known_forget(Operand o)
{
    int i = known_find(o);
    if (i >= 0) {
        known_count--;
        known_name[i] = known_name[known_count];
        known_value[i] = known_value[known_count];
    }
}

static void known_set(Operand o, Operand value)
{
    known_forget(o);
    if (known_count < MAX_KNOWN) {
        known_name[known_count] = o;
        known_value[known_count] = value;
        known_count++;
    }
}

/* Forgets every entry whose name or value is o (o was just changed). */
static void known_forget_all(Operand o)
{
    for (int i = known_count - 1; i >= 0; i--) {
        if (opd_equal(known_name[i], o) || opd_equal(known_value[i], o)) {
            known_count--;
            known_name[i] = known_name[known_count];
            known_value[i] = known_value[known_count];
        }
    }
}

/* After a call: forget everything that involves a global variable. */
static void known_forget_globals(void)
{
    for (int i = known_count - 1; i >= 0; i--) {
        if (is_global(known_name[i]) || is_global(known_value[i])) {
            known_count--;
            known_name[i] = known_name[known_count];
            known_value[i] = known_value[known_count];
        }
    }
}

/* Computes "a op b" for two constants. Returns 1 if it could. */
static int fold_binary(BinOp op, Operand a, Operand b, Operand *out)
{
    if (a.kind == OPD_INT && b.kind == OPD_INT) {
        int x = a.u.ival, y = b.u.ival;
        unsigned ux = (unsigned)x, uy = (unsigned)y;   /* wrap on overflow */
        switch (op) {
        case BIN_ADD: *out = opd_int((int)(ux + uy)); return 1;
        case BIN_SUB: *out = opd_int((int)(ux - uy)); return 1;
        case BIN_MUL: *out = opd_int((int)(ux * uy)); return 1;
        case BIN_SHL: *out = opd_int((int)(ux << (uy & 31))); return 1;
        case BIN_DIV:
        case BIN_MOD:
            if (y == 0 || (x == -2147483647 - 1 && y == -1)) {
                return 0;
            }
            *out = opd_int(op == BIN_DIV ? x / y : x % y);
            return 1;
        case BIN_EQ: *out = opd_bool(x == y); return 1;
        case BIN_NE: *out = opd_bool(x != y); return 1;
        case BIN_LT: *out = opd_bool(x < y);  return 1;
        case BIN_LE: *out = opd_bool(x <= y); return 1;
        case BIN_GT: *out = opd_bool(x > y);  return 1;
        case BIN_GE: *out = opd_bool(x >= y); return 1;
        default: return 0;
        }
    }

    if (a.kind == OPD_FLOAT && b.kind == OPD_FLOAT) {
        double x = a.u.fval, y = b.u.fval;
        switch (op) {
        case BIN_ADD: *out = opd_float(x + y); return 1;
        case BIN_SUB: *out = opd_float(x - y); return 1;
        case BIN_MUL: *out = opd_float(x * y); return 1;
        case BIN_DIV:
            if (y == 0.0) {
                return 0;
            }
            *out = opd_float(x / y);
            return 1;
        case BIN_EQ: *out = opd_bool(x == y); return 1;
        case BIN_NE: *out = opd_bool(x != y); return 1;
        case BIN_LT: *out = opd_bool(x < y);  return 1;
        case BIN_LE: *out = opd_bool(x <= y); return 1;
        case BIN_GT: *out = opd_bool(x > y);  return 1;
        case BIN_GE: *out = opd_bool(x >= y); return 1;
        default: return 0;
        }
    }

    if (a.kind == OPD_BOOL && b.kind == OPD_BOOL) {
        if (op == BIN_EQ) { *out = opd_bool(a.u.ival == b.u.ival); return 1; }
        if (op == BIN_NE) { *out = opd_bool(a.u.ival != b.u.ival); return 1; }
    }
    return 0;
}

static int fold_unary(TacUnOp op, Operand a, Operand *out)
{
    if (op == TAC_NEG && a.kind == OPD_INT) {
        *out = opd_int((int)(0u - (unsigned)a.u.ival));
        return 1;
    }
    if (op == TAC_NEG && a.kind == OPD_FLOAT) {
        *out = opd_float(-a.u.fval);
        return 1;
    }
    if (op == TAC_NOT && a.kind == OPD_BOOL) {
        *out = opd_bool(!a.u.ival);
        return 1;
    }
    if (op == TAC_ITOF && a.kind == OPD_INT) {
        *out = opd_float((double)a.u.ival);
        return 1;
    }
    return 0;
}

/* Counters for the report. */
typedef struct {
    int folded;        /* operations computed at compile time */
    int constants;     /* uses replaced by a constant          */
    int copies;        /* uses replaced by another variable    */
    int branches;      /* conditional jumps decided            */
} FoldStats;

/* One folding sweep over the function. Returns 1 if anything changed. */
static int fold_sweep(TacFunc *f, FoldStats *st)
{
    int changed = 0;
    known_count = 0;

    for (int i = 0; i < f->count; i++) {
        Tac *t = &f->code[i];
        Operand result;

        if (t->kind == TAC_LABEL) {
            known_count = 0;    /* control can arrive here from elsewhere */
            continue;
        }

        /* Propagate: replace operands whose value is known. */
        Operand *ops[2];
        int n = read_operands(t, ops);
        for (int j = 0; j < n; j++) {
            int k = known_find(*ops[j]);
            if (k >= 0) {
                *ops[j] = known_value[k];
                if (opd_is_const(known_value[k])) {
                    st->constants++;
                } else {
                    st->copies++;
                }
                changed = 1;
            }
        }

        /* Fold. */
        if (t->kind == TAC_BINARY && opd_is_const(t->a) && opd_is_const(t->b)
            && fold_binary(t->binop, t->a, t->b, &result)) {
            make_copy(t, result);
            st->folded++;
            changed = 1;
        } else if (t->kind == TAC_UNARY && opd_is_const(t->a)
                   && fold_unary(t->unop, t->a, &result)) {
            make_copy(t, result);
            st->folded++;
            changed = 1;
        } else if ((t->kind == TAC_IF_FALSE || t->kind == TAC_IF_TRUE)
                   && t->a.kind == OPD_BOOL) {
            int jumps = t->kind == TAC_IF_FALSE ? !t->a.u.ival : t->a.u.ival;
            st->branches++;
            changed = 1;
            if (jumps) {
                t->kind = TAC_GOTO;
                t->a = opd_none();
            } else {
                remove_at(f, i);
                i--;
                continue;
            }
        }

        /* Remember what this instruction writes. Anything that depended on
           the old value of dst is forgotten first. */
        if (writes(t)) {
            known_forget_all(t->dst);
            if (t->kind == TAC_COPY && !opd_equal(t->a, t->dst)) {
                known_set(t->dst, t->a);      /* a constant or a copy */
            }
        }
        if (t->kind == TAC_CALL) {
            known_forget_globals();
        }
    }
    return changed;
}

/* ================================================================
 * Pass 3: strength reduction
 *
 * Replaces operations by cheaper ones that give the same result:
 *   x * 8   ->  x << 3       (int, any power of two)
 *   x * 2.0 ->  x + x        (float)
 *   x * 1, x / 1, x + 0, x - 0  ->  x
 *   x * 0   ->  0            (int)
 * (x + 0.0 is not simplified for floats: -0.0 + 0.0 is 0.0, not -0.0.)
 * ================================================================ */

static int is_int(Operand o, int v)      { return o.kind == OPD_INT && o.u.ival == v; }
static int is_float(Operand o, double v) { return o.kind == OPD_FLOAT && o.u.fval == v; }

/* If v is 2^k with k >= 1, returns k; otherwise -1. */
static int power_of_two(Operand o)
{
    if (o.kind != OPD_INT || o.u.ival < 2) {
        return -1;
    }
    int v = o.u.ival, k = 0;
    while ((v & 1) == 0) {
        v >>= 1;
        k++;
    }
    return v == 1 ? k : -1;
}

static int reduce_instr(Tac *t)
{
    Operand a = t->a, b = t->b;
    int k;

    if (t->kind != TAC_BINARY) {
        return 0;
    }

    if (t->type == TYPE_INT) {
        switch (t->binop) {
        case BIN_ADD:
            if (is_int(b, 0)) { make_copy(t, a); return 1; }
            if (is_int(a, 0)) { make_copy(t, b); return 1; }
            break;
        case BIN_SUB:
            if (is_int(b, 0)) { make_copy(t, a); return 1; }
            break;
        case BIN_MUL:
            if (is_int(b, 1)) { make_copy(t, a); return 1; }
            if (is_int(a, 1)) { make_copy(t, b); return 1; }
            if (is_int(a, 0) || is_int(b, 0)) { make_copy(t, opd_int(0)); return 1; }
            if ((k = power_of_two(b)) > 0) {
                t->binop = BIN_SHL; t->b = opd_int(k); return 1;
            }
            if ((k = power_of_two(a)) > 0) {
                t->binop = BIN_SHL; t->a = b; t->b = opd_int(k); return 1;
            }
            break;
        case BIN_DIV:
            if (is_int(b, 1)) { make_copy(t, a); return 1; }
            break;
        default:
            break;
        }
    } else if (t->type == TYPE_FLOAT) {
        switch (t->binop) {
        case BIN_SUB:
            if (is_float(b, 0.0)) { make_copy(t, a); return 1; }
            break;
        case BIN_MUL:
            if (is_float(b, 1.0)) { make_copy(t, a); return 1; }
            if (is_float(a, 1.0)) { make_copy(t, b); return 1; }
            if (is_float(b, 2.0)) { t->binop = BIN_ADD; t->b = a; return 1; }
            if (is_float(a, 2.0)) { t->binop = BIN_ADD; t->a = b; return 1; }
            break;
        case BIN_DIV:
            if (is_float(b, 1.0)) { make_copy(t, a); return 1; }
            break;
        default:
            break;
        }
    }
    return 0;
}

/* ================================================================
 * Pass 4: loop-invariant code motion
 *
 * A computation inside a loop whose operands do not change while the loop
 * runs gives the same result every iteration. It is moved in front of the
 * loop, so it runs once:
 *
 *     L1:                              t2 = n * 4
 *       t1 = i < 10                  L1:
 *       ifFalse t1 goto L2             t1 = i < 10
 *       t2 = n * 4          ->         ifFalse t1 goto L2
 *       x = x + t2                     x = x + t2
 *       ...                            ...
 *
 * Only instructions that are safe to move are moved:
 *   - a pure computation (no call, no print) into a temporary that is
 *     written only once. Moving it cannot change any variable, even if the
 *     loop runs zero times;
 *   - every operand is a constant, or a variable/temporary that is not
 *     written inside the loop (and not a global if the loop has a call);
 *   - no division or % unless the divisor is a constant other than 0 and
 *     -1 (so moving it cannot introduce a division-by-zero error).
 * Inner loops are handled first, so code can move out of several loops.
 * ================================================================ */

static int loop_has_call(const TacFunc *f, Loop L)
{
    for (int i = L.head; i <= L.back; i++) {
        if (f->code[i].kind == TAC_CALL) {
            return 1;
        }
    }
    return 0;
}

static int invariant(const TacFunc *f, Loop L, Operand o, int has_call)
{
    if (o.kind == OPD_NONE || opd_is_const(o)) {
        return 1;
    }
    if (has_call && is_global(o)) {
        return 0;
    }
    return !written_in(f, L.head, L.back, o);
}

static int can_hoist(const TacFunc *f, Loop L, int i, int has_call)
{
    const Tac *t = &f->code[i];

    if (!is_pure(t) || t->dst.kind != OPD_TEMP || count_writes(f, t->dst) != 1) {
        return 0;
    }
    if (t->kind == TAC_BINARY && (t->binop == BIN_DIV || t->binop == BIN_MOD)) {
        if (!opd_is_const(t->b) || is_int(t->b, 0) || is_int(t->b, -1)
            || is_float(t->b, 0.0)) {
            return 0;
        }
    }
    return invariant(f, L, t->a, has_call) && invariant(f, L, t->b, has_call);
}

static void licm_function(TacFunc *f, int *moved)
{
    Loop loops[MAX_LOOPS];
    int progress = 1;

    while (progress) {
        progress = 0;
        int n = find_loops(f, loops);

        for (int k = 0; k < n && !progress; k++) {
            Loop L = loops[k];
            int has_call = loop_has_call(f, L);

            for (int i = L.head + 1; i < L.back; i++) {
                if (can_hoist(f, L, i, has_call)) {
                    Tac t = f->code[i];
                    remove_at(f, i);
                    insert_at(f, L.head, t);   /* just before the label */
                    (*moved)++;
                    progress = 1;              /* indices changed: rescan */
                    break;
                }
            }
        }
    }
}

/* ================================================================
 * Pass 5: dead code elimination
 *
 * Removes code that has no effect on the program's output:
 *   - unreachable code: everything after "goto" or "return" up to the
 *     next label;
 *   - jumps to the very next instruction ("goto L2" right before "L2:");
 *   - labels that no jump uses (they are not counted, but removing them
 *     joins basic blocks, which helps constant propagation);
 *   - assignments whose result is never read: a temporary or local
 *     variable that no instruction uses. (Globals are kept: other
 *     functions may read them.) A call whose result is unused keeps the
 *     call (it may print) but drops the result;
 *   - dead stores: a value that is overwritten later in the same basic
 *     block before anything reads it ("t2 = 1 ... t2 = i % 2");
 *   - copies of a variable to itself (x = x).
 * This repeats until nothing changes, because removing one instruction can
 * make another one dead.
 * ================================================================ */

/*
 * Is the value written by code[i] overwritten before it is read? Only the
 * rest of the basic block is examined: at a label or jump the answer is
 * "don't know", so the store is kept. The same goes for a call if the
 * variable is global (the called function may read it), and for return.
 */
static int overwritten_before_read(const TacFunc *f, int i)
{
    Operand x = f->code[i].dst;

    for (int j = i + 1; j < f->count; j++) {
        const Tac *t = &f->code[j];
        if (reads(t, x)) {
            return 0;
        }
        if (t->kind == TAC_LABEL || is_jump(t) || t->kind == TAC_RETURN
            || (t->kind == TAC_CALL && is_global(x))) {
            return 0;
        }
        if (writes(t) && opd_equal(t->dst, x)) {
            return 1;
        }
    }
    return 0;
}

static int dce_sweep(TacFunc *f, int *removed)
{
    int changed = 0;

    for (int i = 0; i < f->count; i++) {
        Tac *t = &f->code[i];

        /* Unreachable code after goto / return. */
        if (t->kind == TAC_GOTO || t->kind == TAC_RETURN) {
            while (i + 1 < f->count && f->code[i + 1].kind != TAC_LABEL) {
                remove_at(f, i + 1);
                (*removed)++;
                changed = 1;
            }
        }

        /* A jump to a label that directly follows it (maybe after other
           labels) does nothing. */
        if (is_jump(t)) {
            int j = i + 1;
            while (j < f->count && f->code[j].kind == TAC_LABEL
                   && f->code[j].label != t->label) {
                j++;
            }
            if (j < f->count && f->code[j].kind == TAC_LABEL
                && f->code[j].label == t->label) {
                remove_at(f, i);
                (*removed)++;
                changed = 1;
                i--;
                continue;
            }
        }

        /* A label nobody jumps to. */
        if (t->kind == TAC_LABEL && label_refs(f, t->label) == 0) {
            remove_at(f, i);
            changed = 1;
            i--;
            continue;
        }

        /* x = x */
        if (t->kind == TAC_COPY && opd_equal(t->dst, t->a)) {
            remove_at(f, i);
            (*removed)++;
            changed = 1;
            i--;
            continue;
        }

        /* A dead store: the value is overwritten before it is read. */
        if (is_pure(t) && overwritten_before_read(f, i)) {
            remove_at(f, i);
            (*removed)++;
            changed = 1;
            i--;
            continue;
        }

        /* A result that is never read. */
        if (writes(t) && !is_global(t->dst) && !read_anywhere(f, t->dst)) {
            if (t->kind == TAC_CALL) {
                t->dst = opd_none();       /* keep the call itself */
            } else {
                remove_at(f, i);
                (*removed)++;
                i--;
            }
            changed = 1;
        }
    }
    return changed;
}

/* ================================================================
 * Running the passes and the report
 * ================================================================ */

int opt_parse_passes(const char *list, OptPasses *p)
{
    char buf[256];
    memset(p, 0, sizeof *p);

    if (strcmp(list, "none") == 0) {
        return 0;
    }
    if (strcmp(list, "all") == 0) {
        p->unroll = p->fold = p->strength = p->licm = p->dce = 1;
        return 0;
    }

    snprintf(buf, sizeof buf, "%s", list);
    for (char *name = strtok(buf, ","); name != NULL; name = strtok(NULL, ",")) {
        if (strcmp(name, "unroll") == 0)        p->unroll = 1;
        else if (strcmp(name, "fold") == 0)     p->fold = 1;
        else if (strcmp(name, "strength") == 0) p->strength = 1;
        else if (strcmp(name, "licm") == 0)     p->licm = 1;
        else if (strcmp(name, "dce") == 0)      p->dce = 1;
        else return -1;
    }
    return 0;
}

/* Calls fn on the init code and on every function. */
#define FOR_EACH_FUNC(p, f, body)                          \
    do {                                                   \
        TacFunc *f = (p)->init;                            \
        body;                                              \
        for (int fi_ = 0; fi_ < (p)->func_count; fi_++) {  \
            f = (p)->funcs[fi_];                           \
            body;                                          \
        }                                                  \
    } while (0)

static void report_row(int report, const char *pass, int before, int after,
                       const char *details)
{
    char change[16];

    if (!report) {
        return;
    }
    if (after == before) {
        snprintf(change, sizeof change, "0");
    } else {
        snprintf(change, sizeof change, "%+d", after - before);
    }
    printf("%-28s %6d %6d %7s   %s\n", pass, before, after, change, details);
}

static const char *plural(int n)
{
    return n == 1 ? "" : "s";
}

void optimize_program(TacProgram *p, const OptPasses *passes, int report)
{
    char details[256];
    int start = tac_program_count(p);
    int before;

    prog = p;

    if (report) {
        printf("%-28s %6s %6s %7s   %s\n", "PASS", "BEFORE", "AFTER", "CHANGE", "DETAILS");
        printf("%-28s %6s %6s %7s   %s\n", "----", "------", "-----", "------", "-------");
    }

    /* 1. Loop unrolling */
    before = tac_program_count(p);
    if (passes->unroll) {
        int full = 0, partial = 0, iterations = 0;
        FOR_EACH_FUNC(p, f, unroll_function(f, &full, &partial, &iterations));
        if (full + partial == 0) {
            snprintf(details, sizeof details, "no loop with constant bounds");
        } else {
            snprintf(details, sizeof details,
                     "%d loop%s fully unrolled (%d iteration%s), %d partially",
                     full, plural(full), iterations, plural(iterations), partial);
        }
        report_row(report, "loop unrolling", before, tac_program_count(p), details);
    } else {
        report_row(report, "loop unrolling", before, before, "(skipped)");
    }

    /* 2. Constant folding */
    before = tac_program_count(p);
    if (passes->fold) {
        FoldStats st = { 0, 0, 0, 0 };
        FOR_EACH_FUNC(p, f, while (fold_sweep(f, &st)) {});
        snprintf(details, sizeof details,
                 "%d folded, %d constant%s + %d cop%s propagated, %d branch%s decided",
                 st.folded, st.constants, plural(st.constants),
                 st.copies, st.copies == 1 ? "y" : "ies",
                 st.branches, st.branches == 1 ? "" : "es");
        report_row(report, "constant folding", before, tac_program_count(p), details);
    } else {
        report_row(report, "constant folding", before, before, "(skipped)");
    }

    /* 3. Strength reduction */
    before = tac_program_count(p);
    if (passes->strength) {
        int reduced = 0;
        FOR_EACH_FUNC(p, f,
            for (int i = 0; i < f->count; i++) reduced += reduce_instr(&f->code[i]));
        snprintf(details, sizeof details, "%d operation%s made cheaper",
                 reduced, plural(reduced));
        report_row(report, "strength reduction", before, tac_program_count(p), details);
    } else {
        report_row(report, "strength reduction", before, before, "(skipped)");
    }

    /* 4. Loop-invariant code motion */
    before = tac_program_count(p);
    if (passes->licm) {
        int moved = 0;
        FOR_EACH_FUNC(p, f, licm_function(f, &moved));
        snprintf(details, sizeof details, "%d instruction%s moved out of loops",
                 moved, plural(moved));
        report_row(report, "loop-invariant code motion", before, tac_program_count(p), details);
    } else {
        report_row(report, "loop-invariant code motion", before, before, "(skipped)");
    }

    /* 5. Dead code elimination */
    before = tac_program_count(p);
    if (passes->dce) {
        int removed = 0;
        FOR_EACH_FUNC(p, f, while (dce_sweep(f, &removed)) {});
        snprintf(details, sizeof details, "%d instruction%s removed",
                 removed, plural(removed));
        report_row(report, "dead code elimination", before, tac_program_count(p), details);
    } else {
        report_row(report, "dead code elimination", before, before, "(skipped)");
    }

    /* 6. Repeat folding, strength reduction and DCE until nothing changes.
       Each can create work for the others. For example, DCE removes the
       unreachable side of a decided branch and its unused label, which
       joins two basic blocks, so folding can propagate constants further.
       That may decide another branch, and so on. This matters most after
       full loop unrolling. */
    if (passes->fold && passes->dce) {
        FoldStats st = { 0, 0, 0, 0 };
        int reduced = 0, removed = 0, rounds = 0;
        int changed = 1;

        before = tac_program_count(p);
        while (changed && rounds < 20) {
            changed = 0;
            FOR_EACH_FUNC(p, f,
                while (fold_sweep(f, &st)) changed = 1;
                if (passes->strength) {
                    for (int i = 0; i < f->count; i++) {
                        if (reduce_instr(&f->code[i])) { reduced++; changed = 1; }
                    }
                }
                while (dce_sweep(f, &removed)) changed = 1);
            if (changed) {
                rounds++;
            }
        }
        snprintf(details, sizeof details,
                 "%d more round%s: %d folded, %d propagated, %d branch%s, "
                 "%d reduced, %d removed",
                 rounds, plural(rounds), st.folded, st.constants + st.copies,
                 st.branches, st.branches == 1 ? "" : "es", reduced, removed);
        report_row(report, "repeat until stable", before,
                   tac_program_count(p), details);
    }

    report_row(report, "total", start, tac_program_count(p), "");
}
