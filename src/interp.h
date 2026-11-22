/*
 * interp.h -- An interpreter for three-address code (--run).
 *
 * This is not one of the required compiler phases. It exists to TEST the
 * compiler: running a program before and after optimization must print the
 * same output, which shows that the optimizations preserve its meaning.
 */
#ifndef INTERP_H
#define INTERP_H

#include "tac.h"

/* Runs the program: the "init" code, then main(). Program output goes to
   stdout, followed by the number of instructions executed. Returns 0 on
   success, 1 on a runtime error (reported on stderr). */
int interp_run(const TacProgram *p);

#endif /* INTERP_H */
