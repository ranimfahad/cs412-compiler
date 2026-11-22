/*
 * lexer.l -- Phase 1: Lexical analyzer for MiniLang (Flex specification).
 *
 * The scanner turns the source text into a stream of tokens. For each token
 * it records the line and column where the token starts (tok_line, tok_col),
 * so later phases can report errors at exact positions.
 *
 * What it handles:
 *   - Keywords, identifiers, int/float/bool literals
 *   - Operators (+ - * / % = == != < <= > >= && || !) and delimiters
 *   - Single-line comments (// ...) and block comments (slash-star ... star-slash)
 *   - Whitespace, including Windows line endings (\r\n)
 *
 * Lexical errors (reported, then scanning continues):
 *   - Invalid characters such as @, #, $
 *   - A single & or | (probably a typo for && or ||)
 *   - Malformed numbers such as 12abc or 3.
 *   - Integer literals that do not fit in an int
 *   - An unterminated block comment
 *
 * For the parser (Phase 2), each token also gets:
 *   - a value in yylval: the name of an identifier, or the value of a literal
 *   - a position in yylloc: the line and column where the token starts
 *
 * How Flex chooses a rule: it uses the LONGEST match. If two rules match the
 * same length, the rule listed FIRST wins. That is why the keyword rules come
 * before the identifier rule: "int" matches both, and the keyword wins, but
 * "integer" is longer as an identifier, so it becomes an IDENT.
 */

%{
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>

#include "ast.h"      /* xstrdup() */
#include "tokens.h"   /* token codes, yylval and yylloc (from Bison) */
#include "lexer.h"

/* Position of the token just matched (exported through lexer.h). */
int tok_line = 1;
int tok_col  = 1;

int lex_error_count = 0;

/* When set, lexical errors are neither printed nor counted. */
static int lex_quiet = 0;

/* Position of the NEXT character to be read. */
static int cur_line = 1;
static int cur_col  = 1;

/* Where the current block comment started, for "unterminated" errors. */
static int comment_line;
static int comment_col;

/*
 * Called before every rule's action (via YY_USER_ACTION below). It saves the
 * start position of the matched text, then advances the position past it.
 * Matched text can contain newlines (whitespace, comments), so we walk it
 * character by character.
 */
static void update_position(void)
{
    tok_line = cur_line;
    tok_col  = cur_col;
    for (const char *p = yytext; *p != '\0'; p++) {
        if (*p == '\n') {
            cur_line++;
            cur_col = 1;
        } else {
            cur_col++;
        }
    }

    /* Tell the parser where this token is (used by @1, @2, ... in parser.y). */
    yylloc.first_line   = tok_line;
    yylloc.first_column = tok_col;
    yylloc.last_line    = cur_line;
    yylloc.last_column  = cur_col - 1;
}

/* At end of input, point the location just past the last character, so
   "unexpected end of file" errors show where the file ends. */
static void set_eof_position(void)
{
    tok_line = cur_line;
    tok_col  = cur_col;
    yylloc.first_line = yylloc.last_line = cur_line;
    yylloc.first_column = yylloc.last_column = cur_col;
}

#define YY_USER_ACTION update_position();

/*
 * Converts an integer literal to its value. If it does not fit in a C int,
 * reports an error and returns INT_MAX so parsing can continue.
 */
static int int_value(void)
{
    errno = 0;
    long value = strtol(yytext, NULL, 10);
    if (errno == ERANGE || value > INT_MAX) {
        lex_error(tok_line, tok_col,
                  "integer literal '%s' is too large (max %d)", yytext, INT_MAX);
        return INT_MAX;
    }
    return (int)value;
}

/* Reports a character that cannot start any token. */
static void invalid_char(void)
{
    unsigned char c = (unsigned char)yytext[0];
    if (isprint(c)) {
        lex_error(tok_line, tok_col, "invalid character '%c'", c);
    } else {
        lex_error(tok_line, tok_col, "invalid character (byte 0x%02X)", c);
    }
}
%}

 /*
  * Options:
  *   noyywrap          - there is only one input file, so no yywrap() needed
  *   nounput, noinput  - we don't use these helpers (avoids unused warnings)
  *   never-interactive - always read the input in blocks (we read files)
  */
%option noyywrap nounput noinput never-interactive

 /* Start condition for the inside of a block comment. %x = exclusive:
    while in COMMENT, only rules prefixed with <COMMENT> are active. */
%x COMMENT

 /* ---- Named regular expressions ---- */
DIGIT       [0-9]
LETTER      [A-Za-z_]
IDENT       {LETTER}({LETTER}|{DIGIT})*
INTEGER     {DIGIT}+
EXPONENT    [eE][+-]?{DIGIT}+
FLOAT       ({DIGIT}+"."{DIGIT}+{EXPONENT}?)|({DIGIT}+{EXPONENT})

%%

 /* ---------------- Comments and whitespace (no token) ---------------- */

"//"[^\n]*              { /* single-line comment: skip to end of line */ }

"/*"                    {
                            comment_line = tok_line;
                            comment_col  = tok_col;
                            BEGIN(COMMENT);
                        }
<COMMENT>"*/"           { BEGIN(INITIAL); }
<COMMENT>[^*\n]+        { /* comment text: skip */ }
<COMMENT>"*"            { /* a '*' that does not end the comment */ }
<COMMENT>\n             { /* newline inside a comment */ }
<COMMENT><<EOF>>        {
                            lex_error(comment_line, comment_col,
                                      "unterminated block comment");
                            BEGIN(INITIAL);
                            set_eof_position();
                            return YYEOF;
                        }

<INITIAL><<EOF>>        { set_eof_position(); return YYEOF; }

[ \t\r\n]+              { /* whitespace: skip (\r handles CRLF files) */ }

 /* ---------------- Keywords (must come before IDENT) ---------------- */

"int"                   { return KW_INT; }
"float"                 { return KW_FLOAT; }
"bool"                  { return KW_BOOL; }
"void"                  { return KW_VOID; }
"if"                    { return KW_IF; }
"else"                  { return KW_ELSE; }
"while"                 { return KW_WHILE; }
"for"                   { return KW_FOR; }
"return"                { return KW_RETURN; }
"print"                 { return KW_PRINT; }
"true"|"false"          { yylval.ival = (yytext[0] == 't'); return BOOL_LIT; }

 /* ---------------- Numbers ---------------- */

{FLOAT}                 { yylval.fval = strtod(yytext, NULL); return FLOAT_LIT; }
{INTEGER}               { yylval.ival = int_value(); return INT_LIT; }

 /* A number immediately followed by letters, e.g. "12abc" or "1.5f".
    This is longer than the valid number, so it wins and we report it. */
{DIGIT}+("."{DIGIT}+)?{LETTER}({LETTER}|{DIGIT})*   {
                            lex_error(tok_line, tok_col,
                                      "malformed number '%s'", yytext);
                        }

 /* A float with no digits after the point, e.g. "3." */
{DIGIT}+"."             {
                            lex_error(tok_line, tok_col,
                                      "malformed float '%s' (expected digits after '.')",
                                      yytext);
                        }

 /* ---------------- Identifiers ---------------- */

 /* The parser takes ownership of the copied name (yytext is reused). */
{IDENT}                 { yylval.sval = xstrdup(yytext); return IDENT; }

 /* ---------------- Operators ---------------- */
 /* Two-character operators are matched before their one-character
    prefixes by the longest-match rule ("<=" beats "<"). */

"=="                    { return OP_EQ; }
"!="                    { return OP_NE; }
"<="                    { return OP_LE; }
">="                    { return OP_GE; }
"&&"                    { return OP_AND; }
"||"                    { return OP_OR; }

"+"                     { return OP_PLUS; }
"-"                     { return OP_MINUS; }
"*"                     { return OP_MUL; }
"/"                     { return OP_DIV; }
"%"                     { return OP_MOD; }
"="                     { return OP_ASSIGN; }
"<"                     { return OP_LT; }
">"                     { return OP_GT; }
"!"                     { return OP_NOT; }

 /* ---------------- Delimiters ---------------- */

"("                     { return LPAREN; }
")"                     { return RPAREN; }
"{"                     { return LBRACE; }
"}"                     { return RBRACE; }
";"                     { return SEMI; }
","                     { return COMMA; }

 /* ---------------- Errors ---------------- */

"&"                     { lex_error(tok_line, tok_col,
                                    "invalid character '&' (did you mean '&&'?)"); }
"|"                     { lex_error(tok_line, tok_col,
                                    "invalid character '|' (did you mean '||'?)"); }

 /* Anything else is not part of MiniLang. '.' matches any one character
    except newline, and newlines are already handled above. */
.                       { invalid_char(); }

%%

/*
 * Prints a lexical error to stderr and counts it.
 * stdout is flushed first so that, when both streams go to the same place,
 * errors appear in the right order relative to the token listing.
 */
void lex_error(int line, int col, const char *fmt, ...)
{
    va_list args;

    if (lex_quiet) {
        return;
    }

    fflush(stdout);
    fprintf(stderr, "lexical error (line %d, col %d): ", line, col);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");

    lex_error_count++;
}

void lexer_reset(int quiet)
{
    rewind(yyin);
    yyrestart(yyin);      /* discard Flex's buffered input */
    BEGIN(INITIAL);
    cur_line = 1;
    cur_col  = 1;
    tok_line = 1;
    tok_col  = 1;
    lex_quiet = quiet;
}
