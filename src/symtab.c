/*
 * symtab.c -- Scoped symbol table (Phase 3). See symtab.h.
 *
 * Each scope keeps its symbols in a linked list. Lookups are linear
 * searches; MiniLang programs are small, so this is fast enough and much
 * easier to follow than a hash table.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "symtab.h"

typedef struct Scope {
    char          *name;     /* "global", "main", "main/block@12", ... */
    int            level;    /* 0 = global                             */
    Symbol        *symbols;  /* symbols declared in this scope         */
    struct Scope  *parent;   /* enclosing scope, NULL for global       */
} Scope;

static Scope *current = NULL;      /* innermost open scope */

/* Every symbol ever declared, in order (kept for printing and freeing). */
static Symbol **all_symbols = NULL;
static int      all_count = 0;
static int      all_capacity = 0;

/* Index in all_symbols where the current function's symbols start. */
static int function_start = 0;

/* ================================================================
 * Scopes
 * ================================================================ */

void symtab_init(void)
{
    current = NULL;
    all_symbols = NULL;
    all_count = 0;
    all_capacity = 0;
    function_start = 0;
}

void scope_push(const char *name)
{
    Scope *s = xmalloc(sizeof *s);
    s->name = xstrdup(name);
    s->level = current == NULL ? 0 : current->level + 1;
    s->symbols = NULL;
    s->parent = current;
    current = s;
}

/* The symbols themselves stay alive (they are in all_symbols). */
void scope_pop(void)
{
    Scope *s = current;
    current = s->parent;
    free(s->name);
    free(s);
}

const char *scope_current_name(void)
{
    return current->name;
}

void symtab_begin_function(void)
{
    function_start = all_count;
}

/* ================================================================
 * Unique names for the three-address code
 * ================================================================ */

/* Is this TAC name already used by a global, or by a symbol of the
   function being analyzed? */
static int ir_name_taken(const char *ir_name)
{
    for (int i = 0; i < all_count; i++) {
        const Symbol *s = all_symbols[i];
        if ((s->level == 0 || i >= function_start)
            && strcmp(s->ir_name, ir_name) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Picks the TAC name for a new symbol: its own name if free, otherwise
   name_1, name_2, ... */
static char *choose_ir_name(const char *name, int level)
{
    char buf[256];

    if (level == 0 || !ir_name_taken(name)) {
        return xstrdup(name);
    }
    for (int n = 1; ; n++) {
        snprintf(buf, sizeof buf, "%s_%d", name, n);
        if (!ir_name_taken(buf)) {
            return xstrdup(buf);
        }
    }
}

/* ================================================================
 * Declaring and looking up
 * ================================================================ */

Symbol *symtab_declare(const char *name, SymKind kind, Type type,
                       int line, int col)
{
    Symbol *sym = xmalloc(sizeof *sym);
    memset(sym, 0, sizeof *sym);

    sym->name = xstrdup(name);
    sym->kind = kind;
    sym->type = type;
    sym->line = line;
    sym->col = col;
    sym->level = current->level;
    sym->scope_name = xstrdup(current->name);
    sym->ir_name = choose_ir_name(name, current->level);

    /* Add to the front of the current scope's list. */
    sym->next_in_scope = current->symbols;
    current->symbols = sym;

    /* Remember it in the list of all symbols. */
    if (all_count == all_capacity) {
        all_capacity = all_capacity == 0 ? 16 : all_capacity * 2;
        all_symbols = realloc(all_symbols, all_capacity * sizeof *all_symbols);
        if (all_symbols == NULL) {
            fprintf(stderr, "fatal: out of memory\n");
            exit(2);
        }
    }
    all_symbols[all_count++] = sym;

    return sym;
}

static Symbol *find_in_scope(const Scope *scope, const char *name)
{
    for (Symbol *s = scope->symbols; s != NULL; s = s->next_in_scope) {
        if (strcmp(s->name, name) == 0) {
            return s;
        }
    }
    return NULL;
}

Symbol *symtab_lookup(const char *name)
{
    /* Innermost scope first: this is what makes shadowing work. */
    for (const Scope *scope = current; scope != NULL; scope = scope->parent) {
        Symbol *s = find_in_scope(scope, name);
        if (s != NULL) {
            return s;
        }
    }
    return NULL;
}

Symbol *symtab_lookup_current(const char *name)
{
    return find_in_scope(current, name);
}

/* ================================================================
 * Printing and freeing
 * ================================================================ */

void symbol_type_string(const Symbol *sym, char *buf, int size)
{
    if (sym->kind != SYM_FUNC) {
        snprintf(buf, size, "%s", type_name(sym->type));
        return;
    }

    /* Function type: return(param, param, ...) */
    int len = snprintf(buf, size, "%s(", type_name(sym->type));
    for (int i = 0; i < sym->param_count && len < size; i++) {
        len += snprintf(buf + len, size - len, "%s%s",
                        i > 0 ? ", " : "", type_name(sym->param_types[i]));
    }
    if (len < size) {
        snprintf(buf + len, size - len, ")");
    }
}

static const char *kind_name(SymKind kind)
{
    switch (kind) {
    case SYM_VAR:   return "variable";
    case SYM_PARAM: return "parameter";
    case SYM_FUNC:  return "function";
    }
    return "?";
}

void symtab_print(void)
{
    char type_buf[128];

    printf("%-12s %-10s %-18s %-24s %-6s %-5s %s\n",
           "NAME", "KIND", "TYPE", "SCOPE", "LEVEL", "LINE", "TAC NAME");
    printf("%-12s %-10s %-18s %-24s %-6s %-5s %s\n",
           "----", "----", "----", "-----", "-----", "----", "--------");

    for (int i = 0; i < all_count; i++) {
        const Symbol *s = all_symbols[i];
        symbol_type_string(s, type_buf, sizeof type_buf);
        printf("%-12s %-10s %-18s %-24s %-6d %-5d %s\n",
               s->name, kind_name(s->kind), type_buf, s->scope_name,
               s->level, s->line, s->ir_name);
    }
    printf("(%d symbol%s)\n", all_count, all_count == 1 ? "" : "s");
}

void symtab_free(void)
{
    while (current != NULL) {
        scope_pop();
    }
    for (int i = 0; i < all_count; i++) {
        Symbol *s = all_symbols[i];
        free(s->name);
        free(s->param_types);
        free(s->scope_name);
        free(s->ir_name);
        free(s);
    }
    free(all_symbols);
    symtab_init();
}
