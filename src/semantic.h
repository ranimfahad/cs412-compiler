/*
 * semantic.h -- Phase 3: semantic analysis.
 *
 * Walks the AST and checks the rules that the grammar cannot express:
 * declarations, scopes and types. Along the way it fills in
 *   - Node.type for every expression, and
 *   - Node.sym  for every name (which declaration it refers to),
 * which code generation (Phase 4) relies on.
 */
#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "ast.h"

/* Number of semantic errors reported. */
extern int semantic_error_count;

/* Analyzes the whole program. The symbol table (symtab.h) must have been
   initialized with symtab_init(); it is left filled in for --symbols. */
void semantic_analyze(Node *program);

#endif /* SEMANTIC_H */
