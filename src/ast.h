/*
 * ast.h -- Abstract Syntax Tree for MiniLang (built in Phase 2).
 *
 * The parser builds one Node for each construct in the program. A Node has a
 * kind (what construct it is), a source position, and kind-specific fields
 * stored in a union.
 *
 * Lists (declarations, statements, parameters, arguments) are stored in a
 * NodeList, a growable array of Node pointers.
 *
 * Example: "x = a + 2;" becomes
 *
 *     Assign x
 *       Binary +
 *         Var a
 *         Int 2
 */
#ifndef AST_H
#define AST_H

#include <stddef.h>   /* size_t */

/* ---- Types of the language (also used by later phases) ---- */
typedef enum {
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_BOOL,
    TYPE_VOID,     /* function return type only */
    TYPE_ERROR     /* marks an expression that already had a type error */
} Type;

/* ---- Operators ---- */
typedef enum {
    BIN_ADD, BIN_SUB, BIN_MUL, BIN_DIV, BIN_MOD,     /* arithmetic */
    BIN_EQ, BIN_NE, BIN_LT, BIN_LE, BIN_GT, BIN_GE,  /* relational */
    BIN_AND, BIN_OR,                                 /* logical    */
    BIN_SHL        /* x << n: not in MiniLang; only created by the
                      optimizer (strength reduction: x * 8 -> x << 3) */
} BinOp;

typedef enum {
    UN_NEG,        /* -x */
    UN_NOT         /* !x */
} UnOp;

/* ---- Node kinds ---- */
typedef enum {
    /* Declarations */
    N_PROGRAM,     /* the whole program: list of global declarations */
    N_VAR_DECL,    /* int x = 5;                                     */
    N_FUNC_DECL,   /* int f(int a) { ... }                           */
    N_PARAM,       /* one function parameter: int a                  */

    /* Statements */
    N_BLOCK,       /* { stmt* }                                      */
    N_ASSIGN,      /* x = expr;                                      */
    N_IF,          /* if (cond) stmt [else stmt]                     */
    N_WHILE,       /* while (cond) stmt                              */
    N_FOR,         /* for (init; cond; update) stmt                  */
    N_RETURN,      /* return [expr];                                 */
    N_PRINT,       /* print(expr);                                   */
    N_EXPR_STMT,   /* expr;  (usually a function call)               */

    /* Expressions */
    N_BINARY,      /* left op right                                  */
    N_UNARY,       /* op operand                                     */
    N_INT_LIT,     /* 42                                             */
    N_FLOAT_LIT,   /* 3.14                                           */
    N_BOOL_LIT,    /* true / false                                   */
    N_VAR,         /* x  (use of a variable)                         */
    N_CALL         /* f(args)                                        */
} NodeKind;

typedef struct Node Node;
struct Symbol;   /* defined in symtab.h */

/* A growable array of nodes. */
typedef struct {
    Node **items;
    int count;
    int capacity;
} NodeList;

struct Node {
    NodeKind kind;
    int line;              /* source position where the construct starts */
    int col;
    Type type;             /* type of an expression; set in Phase 3 */

    /* For nodes that name a variable or function (VarDecl, FuncDecl, Param,
       Assign, Var, Call): the symbol the name refers to. Set in Phase 3
       (scope resolution) and used by code generation in Phase 4. */
    struct Symbol *sym;

    union {
        struct { NodeList *decls; }                                program;
        struct { Type type; char *name; Node *init; }              var_decl;   /* init may be NULL */
        struct { Type ret_type; char *name; NodeList *params; Node *body; } func_decl;
        struct { Type type; char *name; }                          param;

        struct { NodeList *stmts; }                                block;
        struct { char *name; Node *value; }                        assign;
        struct { Node *cond; Node *then_stmt; Node *else_stmt; }   if_stmt;    /* else may be NULL */
        struct { Node *cond; Node *body; }                         while_stmt;
        struct { Node *init; Node *cond; Node *update; Node *body; } for_stmt; /* first three may be NULL */
        struct { Node *value; }                                    return_stmt; /* value may be NULL */
        struct { Node *value; }                                    print_stmt;
        struct { Node *expr; }                                     expr_stmt;

        struct { BinOp op; Node *left; Node *right; }              binary;
        struct { UnOp op; Node *operand; }                         unary;
        int    int_val;
        double float_val;
        int    bool_val;
        struct { char *name; }                                     var;
        struct { char *name; NodeList *args; }                     call;
    } u;
};

/* ---- Lists ---- */
NodeList *list_new(void);
NodeList *list_append(NodeList *list, Node *node);   /* ignores NULL nodes */

/* ---- Constructors (one per node kind) ---- */
Node *ast_program(NodeList *decls);
Node *ast_var_decl(int line, int col, Type type, char *name, Node *init);
Node *ast_func_decl(int line, int col, Type ret_type, char *name,
                    NodeList *params, Node *body);
Node *ast_param(int line, int col, Type type, char *name);

Node *ast_block(int line, int col, NodeList *stmts);
Node *ast_assign(int line, int col, char *name, Node *value);
Node *ast_if(int line, int col, Node *cond, Node *then_stmt, Node *else_stmt);
Node *ast_while(int line, int col, Node *cond, Node *body);
Node *ast_for(int line, int col, Node *init, Node *cond, Node *update, Node *body);
Node *ast_return(int line, int col, Node *value);
Node *ast_print(int line, int col, Node *value);
Node *ast_expr_stmt(int line, int col, Node *expr);

Node *ast_binary(int line, int col, BinOp op, Node *left, Node *right);
Node *ast_unary(int line, int col, UnOp op, Node *operand);
Node *ast_int(int line, int col, int value);
Node *ast_float(int line, int col, double value);
Node *ast_bool(int line, int col, int value);
Node *ast_var(int line, int col, char *name);
Node *ast_call(int line, int col, char *name, NodeList *args);

/* ---- Utilities ---- */
void ast_print_tree(const Node *root);   /* used by --ast */
void ast_free(Node *node);
void list_free(NodeList *list);

const char *type_name(Type t);           /* "int", "float", ... */
const char *binop_name(BinOp op);        /* "+", "<=", "&&", ... */
const char *unop_name(UnOp op);          /* "-", "!" */
void format_float(double v, char *buf, int size);   /* 2.0 -> "2.0" */

/* Memory helpers: exit with an error instead of returning NULL.
   (strdup() is not part of standard C, so we provide our own.) */
void *xmalloc(size_t size);
char *xstrdup(const char *s);

#endif /* AST_H */
