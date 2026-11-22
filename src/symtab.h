/*
 * symtab.h -- Scoped symbol table (Phase 3).
 *
 * The symbol table maps names to what they mean (a variable, a parameter or
 * a function) in the current scope.
 *
 * Scopes form a stack:
 *
 *     global              level 0: global variables and all functions
 *       fact              level 1: parameters and top-level locals of fact
 *         fact/block@12   level 2: variables of a nested { } block
 *
 * scope_push() opens a scope when the analyzer enters a function or block,
 * and scope_pop() closes it at the end. A lookup searches from the innermost
 * scope outward, so an inner declaration hides (shadows) an outer one with
 * the same name. Redeclaration is only an error within the SAME scope.
 *
 * Symbols are never freed when their scope closes. Every symbol stays in a
 * list of all symbols, because the AST points to them (Node.sym) and later
 * phases use them. That list is also what --symbols prints.
 */
#ifndef SYMTAB_H
#define SYMTAB_H

#include "ast.h"

typedef enum {
    SYM_VAR,       /* variable (global or local) */
    SYM_PARAM,     /* function parameter         */
    SYM_FUNC       /* function                   */
} SymKind;

typedef struct Symbol {
    char   *name;          /* name in the source program                  */
    SymKind kind;
    Type    type;          /* variable type, or a function's return type  */
    int     line, col;     /* where it was declared                       */

    /* Functions only: parameter types, in order. */
    int     param_count;
    Type   *param_types;

    /* Scope it was declared in (for printing). */
    int     level;         /* 0 = global, 1 = function, 2+ = nested block */
    char   *scope_name;    /* e.g. "global", "main", "main/block@12"      */

    /* Name used in the three-address code. Usually the same as name, but
       a local that has the same name as a global or as another local of
       the same function is renamed (x_1, x_2, ...), so every variable in
       a function's code is unique. */
    char   *ir_name;

    struct Symbol *next_in_scope;   /* the other symbols of its scope */
} Symbol;

/* Starts an empty table (no scopes open). */
void symtab_init(void);

/* Opens a new innermost scope with the given name / closes it. */
void scope_push(const char *name);
void scope_pop(void);

/* Name of the innermost scope, e.g. "main/block@12". */
const char *scope_current_name(void);

/* Call when starting to analyze a function. Unique TAC names (ir_name)
   are chosen per function, so this resets the set of names in use. */
void symtab_begin_function(void);

/*
 * Adds a symbol to the innermost scope and returns it. The caller must
 * first check for a redeclaration with symtab_lookup_current().
 */
Symbol *symtab_declare(const char *name, SymKind kind, Type type,
                       int line, int col);

/* Looks a name up, innermost scope first. Returns NULL if not found. */
Symbol *symtab_lookup(const char *name);

/* Looks a name up in the innermost scope only (for redeclarations). */
Symbol *symtab_lookup_current(const char *name);

/* Prints every symbol ever declared, in declaration order (--symbols). */
void symtab_print(void);

/* Frees all symbols and scopes. */
void symtab_free(void);

/* Writes a function's type, e.g. "int(int, float)", into buf. */
void symbol_type_string(const Symbol *sym, char *buf, int size);

#endif /* SYMTAB_H */
