/*
 * tac.h -- Three-address code (TAC), the intermediate representation.
 *
 * Each instruction has at most three addresses (operands): usually one
 * result and two inputs, e.g.
 *
 *     t1 = a + b          (BINARY)
 *     x = t1              (COPY)
 *     ifFalse t2 goto L1  (IF_FALSE)
 *
 * An operand is a variable, a temporary (t1, t2, ... created by the code
 * generator), or a constant.
 *
 * The code of each function is a growable array of instructions. Phase 5
 * (optimization) works on these arrays.
 */
#ifndef TAC_H
#define TAC_H

#include "ast.h"

/* ---- Operands ---- */
typedef enum {
    OPD_NONE,      /* unused operand slot            */
    OPD_VAR,       /* program variable: x, limit     */
    OPD_TEMP,      /* temporary: t1, t2, ...         */
    OPD_INT,       /* integer constant: 42           */
    OPD_FLOAT,     /* float constant: 2.5            */
    OPD_BOOL       /* boolean constant: true / false */
} OperandKind;

typedef struct {
    OperandKind kind;
    union {
        const char *name;  /* OPD_VAR: points to Symbol.ir_name (not owned) */
        int    temp;       /* OPD_TEMP: the number n in "tn"                */
        int    ival;       /* OPD_INT, OPD_BOOL (0 or 1)                    */
        double fval;       /* OPD_FLOAT                                     */
    } u;
} Operand;

/* ---- Instructions ---- */
typedef enum {
    TAC_COPY,       /* dst = a                                   */
    TAC_BINARY,     /* dst = a op b      (op is a BinOp)         */
    TAC_UNARY,      /* dst = op a        (op is a TacUnOp)       */
    TAC_LABEL,      /* L:                                        */
    TAC_GOTO,       /* goto L                                    */
    TAC_IF_FALSE,   /* ifFalse a goto L  (jump if a is false)    */
    TAC_IF_TRUE,    /* if a goto L       (jump if a is true)     */
    TAC_PARAM,      /* param a           (pass an argument)      */
    TAC_CALL,       /* dst = call f, n   (dst is NONE for void)  */
    TAC_RETURN,     /* return a          (a is NONE for void)    */
    TAC_PRINT       /* print a                                   */
} TacKind;

typedef enum {
    TAC_NEG,        /* dst = -a          */
    TAC_NOT,        /* dst = !a          */
    TAC_ITOF        /* dst = (float) a   (int to float conversion) */
} TacUnOp;

typedef struct {
    TacKind  kind;
    BinOp    binop;     /* TAC_BINARY                              */
    TacUnOp  unop;      /* TAC_UNARY                               */
    Type     type;      /* BINARY, UNARY: the type of the operands
                           (int, float or bool). The optimizer needs
                           it, e.g. x * 8 -> x << 3 is only for ints. */
    Operand  dst;       /* result (COPY, BINARY, UNARY, CALL)      */
    Operand  a, b;      /* inputs                                  */
    int      label;     /* LABEL, GOTO, IF_FALSE, IF_TRUE: Ln      */
    const char *func;   /* CALL: function name (not owned)         */
    int      nargs;     /* CALL: number of 'param' before it       */
} Tac;

/* The code of one function (or of the global initializers). */
typedef struct {
    const char  *name;        /* function name (not owned)               */
    const char **params;      /* parameter TAC names, in order           */
    int          param_count;
    Tac         *code;        /* the instructions                        */
    int          count;
    int          capacity;
    int          next_temp;   /* for new temporaries: t1, t2, ...        */
    int          next_label;  /* for new labels: L1, L2, ...             */
} TacFunc;

typedef struct {
    const char **global_names;  /* global variables (TAC names)          */
    Type        *global_types;
    int          global_count;
    TacFunc     *init;          /* code that initializes the globals     */
    TacFunc    **funcs;         /* one per function, in source order     */
    int          func_count;
} TacProgram;

/* ---- Building ---- */
Operand opd_none(void);
Operand opd_var(const char *name);
Operand opd_temp(int n);
Operand opd_int(int v);
Operand opd_float(double v);
Operand opd_bool(int v);

TacFunc *tac_func_new(const char *name);
void     tac_emit(TacFunc *f, Tac instr);     /* append one instruction */
Operand  tac_new_temp(TacFunc *f);            /* a fresh temporary      */
int      tac_new_label(TacFunc *f);           /* a fresh label number   */

/* ---- Inspecting ---- */
int  opd_is_const(Operand o);
int  opd_equal(Operand x, Operand y);

/* Number of instructions, not counting labels (labels are only markers). */
int  tac_func_count(const TacFunc *f);
int  tac_program_count(const TacProgram *p);

/* ---- Printing and freeing ---- */
void tac_print_operand(Operand o);
void tac_print_instr(const Tac *t);
void tac_print_program(const TacProgram *p);
void tac_func_free(TacFunc *f);
void tac_program_free(TacProgram *p);

#endif /* TAC_H */
