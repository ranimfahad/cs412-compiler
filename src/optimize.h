/*
 * optimize.h -- Phase 5: optimizations on the three-address code.
 *
 * Five passes, run in this order:
 *
 *   1. unroll    loop unrolling (for loops with constant bounds)
 *   2. fold      constant folding and constant propagation
 *   3. strength  strength reduction (cheaper operations)
 *   4. licm      loop-invariant code motion
 *   5. dce       dead code elimination
 *
 * Unrolling comes first because it needs loops in exactly the shape the
 * code generator emits. Its copies of the loop body contain constants that
 * folding can then simplify. Dead code elimination comes last and cleans up
 * what the other passes leave behind (unused variables, unreachable code).
 *
 * Finally, folding, strength reduction and DCE are repeated until the code
 * stops changing, because each can create new work for the others.
 */
#ifndef OPTIMIZE_H
#define OPTIMIZE_H

#include "tac.h"

/* Which passes to run. */
typedef struct {
    int unroll;
    int fold;
    int strength;
    int licm;
    int dce;
} OptPasses;

/*
 * Parses a comma-separated list of pass names ("fold,dce"), or "all" or
 * "none". Returns 0 on success, -1 if a name is unknown.
 */
int opt_parse_passes(const char *list, OptPasses *passes);

/* Optimizes the program in place. If report is set, prints a table with
   the instruction count before and after each pass. */
void optimize_program(TacProgram *p, const OptPasses *passes, int report);

#endif /* OPTIMIZE_H */
