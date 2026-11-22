/*
 * irgen.h -- Phase 4: generating three-address code from the AST.
 */
#ifndef IRGEN_H
#define IRGEN_H

#include "ast.h"
#include "tac.h"

/* Translates a program that passed semantic analysis (so every Node.type
   and Node.sym is set) into three-address code. */
TacProgram *irgen_program(Node *program);

#endif /* IRGEN_H */
