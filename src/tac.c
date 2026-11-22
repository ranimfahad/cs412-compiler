/*
 * tac.c -- Three-address code: building, printing and freeing. See tac.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tac.h"

/* ================================================================
 * Operands
 * ================================================================ */

Operand opd_none(void)            { Operand o; memset(&o, 0, sizeof o); o.kind = OPD_NONE; return o; }
Operand opd_var(const char *name) { Operand o = opd_none(); o.kind = OPD_VAR;   o.u.name = name; return o; }
Operand opd_temp(int n)           { Operand o = opd_none(); o.kind = OPD_TEMP;  o.u.temp = n;    return o; }
Operand opd_int(int v)            { Operand o = opd_none(); o.kind = OPD_INT;   o.u.ival = v;    return o; }
Operand opd_float(double v)       { Operand o = opd_none(); o.kind = OPD_FLOAT; o.u.fval = v;    return o; }
Operand opd_bool(int v)           { Operand o = opd_none(); o.kind = OPD_BOOL;  o.u.ival = !!v;  return o; }

int opd_is_const(Operand o)
{
    return o.kind == OPD_INT || o.kind == OPD_FLOAT || o.kind == OPD_BOOL;
}

int opd_equal(Operand x, Operand y)
{
    if (x.kind != y.kind) {
        return 0;
    }
    switch (x.kind) {
    case OPD_NONE:  return 1;
    case OPD_VAR:   return strcmp(x.u.name, y.u.name) == 0;
    case OPD_TEMP:  return x.u.temp == y.u.temp;
    case OPD_INT:
    case OPD_BOOL:  return x.u.ival == y.u.ival;
    case OPD_FLOAT: return x.u.fval == y.u.fval;
    }
    return 0;
}

/* ================================================================
 * Functions
 * ================================================================ */

TacFunc *tac_func_new(const char *name)
{
    TacFunc *f = xmalloc(sizeof *f);
    memset(f, 0, sizeof *f);
    f->name = name;
    f->next_temp = 1;
    f->next_label = 1;
    return f;
}

void tac_emit(TacFunc *f, Tac instr)
{
    if (f->count == f->capacity) {
        f->capacity = f->capacity == 0 ? 16 : f->capacity * 2;
        f->code = realloc(f->code, f->capacity * sizeof *f->code);
        if (f->code == NULL) {
            fprintf(stderr, "fatal: out of memory\n");
            exit(2);
        }
    }
    f->code[f->count++] = instr;
}

Operand tac_new_temp(TacFunc *f)
{
    return opd_temp(f->next_temp++);
}

int tac_new_label(TacFunc *f)
{
    return f->next_label++;
}

int tac_func_count(const TacFunc *f)
{
    int n = 0;
    for (int i = 0; i < f->count; i++) {
        if (f->code[i].kind != TAC_LABEL) {
            n++;
        }
    }
    return n;
}

int tac_program_count(const TacProgram *p)
{
    int n = tac_func_count(p->init);
    for (int i = 0; i < p->func_count; i++) {
        n += tac_func_count(p->funcs[i]);
    }
    return n;
}

/* ================================================================
 * Printing
 * ================================================================ */

void tac_print_operand(Operand o)
{
    char buf[64];

    switch (o.kind) {
    case OPD_NONE:  printf("_"); break;
    case OPD_VAR:   printf("%s", o.u.name); break;
    case OPD_TEMP:  printf("t%d", o.u.temp); break;
    case OPD_INT:   printf("%d", o.u.ival); break;
    case OPD_BOOL:  printf("%s", o.u.ival ? "true" : "false"); break;
    case OPD_FLOAT:
        format_float(o.u.fval, buf, sizeof buf);
        printf("%s", buf);
        break;
    }
}

/* Prints one instruction (no indentation, no newline). */
void tac_print_instr(const Tac *t)
{
    switch (t->kind) {
    case TAC_COPY:
        tac_print_operand(t->dst);
        printf(" = ");
        tac_print_operand(t->a);
        break;

    case TAC_BINARY:
        tac_print_operand(t->dst);
        printf(" = ");
        tac_print_operand(t->a);
        printf(" %s ", binop_name(t->binop));
        tac_print_operand(t->b);
        break;

    case TAC_UNARY:
        tac_print_operand(t->dst);
        printf(" = %s", t->unop == TAC_NEG ? "-" :
                        t->unop == TAC_NOT ? "!" : "(float) ");
        tac_print_operand(t->a);
        break;

    case TAC_LABEL:
        printf("L%d:", t->label);
        break;

    case TAC_GOTO:
        printf("goto L%d", t->label);
        break;

    case TAC_IF_FALSE:
        printf("ifFalse ");
        tac_print_operand(t->a);
        printf(" goto L%d", t->label);
        break;

    case TAC_IF_TRUE:
        printf("if ");
        tac_print_operand(t->a);
        printf(" goto L%d", t->label);
        break;

    case TAC_PARAM:
        printf("param ");
        tac_print_operand(t->a);
        break;

    case TAC_CALL:
        if (t->dst.kind != OPD_NONE) {
            tac_print_operand(t->dst);
            printf(" = ");
        }
        printf("call %s, %d", t->func, t->nargs);
        break;

    case TAC_RETURN:
        printf("return");
        if (t->a.kind != OPD_NONE) {
            printf(" ");
            tac_print_operand(t->a);
        }
        break;

    case TAC_PRINT:
        printf("print ");
        tac_print_operand(t->a);
        break;
    }
}

/* Prints a function's code: labels at the left margin, the rest indented. */
static void print_code(const TacFunc *f)
{
    for (int i = 0; i < f->count; i++) {
        if (f->code[i].kind != TAC_LABEL) {
            printf("    ");
        }
        tac_print_instr(&f->code[i]);
        printf("\n");
    }
}

static void print_count_line(int n)
{
    printf("    (%d instruction%s)\n", n, n == 1 ? "" : "s");
}

void tac_print_program(const TacProgram *p)
{
    if (p->global_count > 0) {
        printf("globals:\n");
        for (int i = 0; i < p->global_count; i++) {
            printf("    %s %s\n", type_name(p->global_types[i]), p->global_names[i]);
        }
        printf("\n");
    }

    if (p->init->count > 0) {
        printf("init:\n");
        print_code(p->init);
        print_count_line(tac_func_count(p->init));
        printf("\n");
    }

    for (int i = 0; i < p->func_count; i++) {
        const TacFunc *f = p->funcs[i];
        printf("function %s(", f->name);
        for (int j = 0; j < f->param_count; j++) {
            printf("%s%s", j > 0 ? ", " : "", f->params[j]);
        }
        printf("):\n");
        print_code(f);
        print_count_line(tac_func_count(f));
        printf("end %s\n\n", f->name);
    }

    int total = tac_program_count(p);
    printf("Total: %d instruction%s\n", total, total == 1 ? "" : "s");
}

/* ================================================================
 * Freeing
 * ================================================================ */

void tac_func_free(TacFunc *f)
{
    if (f == NULL) {
        return;
    }
    free(f->code);
    free(f->params);
    free(f);
}

void tac_program_free(TacProgram *p)
{
    if (p == NULL) {
        return;
    }
    free(p->global_names);
    free(p->global_types);
    tac_func_free(p->init);
    for (int i = 0; i < p->func_count; i++) {
        tac_func_free(p->funcs[i]);
    }
    free(p->funcs);
    free(p);
}
