/*
 * lexer.h -- Interface to the Flex-generated scanner (lexer.l).
 */
#ifndef LEXER_H
#define LEXER_H

#include <stdio.h>

/* Provided by Flex. */
extern FILE *yyin;       /* input stream the scanner reads from   */
extern char *yytext;     /* text of the token just matched        */
int yylex(void);         /* returns the next token code, 0 at EOF */

/* Position of the token most recently returned by yylex() (1-based).
   The same position is also stored in yylloc for the parser. */
extern int tok_line;
extern int tok_col;

/* Number of lexical errors reported so far. */
extern int lex_error_count;

/* Reports a lexical error at the given position (printed to stderr). */
void lex_error(int line, int col, const char *fmt, ...);

/*
 * Rewinds the input so it can be scanned again from the start.
 * If quiet is set, lexical errors are not reported or counted again (used
 * when --tokens already reported them in an earlier pass).
 */
void lexer_reset(int quiet);

#endif /* LEXER_H */
