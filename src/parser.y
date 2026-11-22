/*
 * parser.y -- Phase 2: Syntax analyzer for MiniLang (Bison specification).
 *
 * Bison builds an LALR(1) parser from this grammar: a bottom-up parser that
 * shifts tokens onto a stack and reduces them to a nonterminal when the
 * right-hand side of a rule is on top of the stack. Each rule's action
 * ({ ... }) builds the AST node for that construct. $$ is the value of the
 * rule's left side, $1, $2, ... are the values of the right side symbols,
 * and @1, @2, ... are their source positions.
 *
 * Conflicts:
 *   %expect 0 makes Bison fail the build if the grammar has ANY
 *   shift/reduce or reduce/reduce conflict. All potential conflicts are
 *   resolved with precedence declarations:
 *     - operator precedence and associativity (%left, %right)
 *     - the "dangling else" (%nonassoc LOWER_THAN_ELSE / "else")
 *
 * Error recovery:
 *   The special token 'error' marks where the parser may resynchronize
 *   after a syntax error. On an error the parser pops states until one can
 *   shift 'error', then skips input tokens until one can follow it (for
 *   example the ';' in "stmt: error ';'"). Then parsing continues, so several
 *   errors can be reported in one run. Bison does not report a new error
 *   until three tokens have been shifted successfully, which prevents a
 *   cascade of messages from a single mistake.
 */

%{
#include <stdio.h>
#include <string.h>

#include "ast.h"
#include "lexer.h"

Node *ast_root = NULL;
int syntax_error_count = 0;

void yyerror(const char *msg);

/* Line and column of a symbol, for AST constructors: POS(@1) */
#define POS(loc) (loc).first_line, (loc).first_column
%}

/* Code placed in the generated header (parser.h), which the lexer and the
   driver include. The %union below uses the AST types. */
%code requires {
#include "ast.h"
}
%code provides {
extern Node *ast_root;          /* root of the AST after yyparse()  */
extern int syntax_error_count;  /* number of syntax errors reported */
}

%locations                      /* track @n positions (set by the lexer)   */
%define parse.error custom      /* we write the messages: see the end of  */
                                /* this file, yyreport_syntax_error()     */
%expect 0                       /* no conflicts allowed                    */

/* The possible types of a symbol's value ($$, $1, ...). */
%union {
    int       ival;
    double    fval;
    char     *sval;
    Node     *node;
    NodeList *list;
    Type      type;
}

/* ---------------- Tokens ----------------
   The quoted string after each token is an alias: it can be used in the
   grammar instead of the name, and it is shown in error messages. */

%token KW_INT    "int"
%token KW_FLOAT  "float"
%token KW_BOOL   "bool"
%token KW_VOID   "void"
%token KW_IF     "if"
%token KW_ELSE   "else"
%token KW_WHILE  "while"
%token KW_FOR    "for"
%token KW_RETURN "return"
%token KW_PRINT  "print"

%token <sval> IDENT     "identifier"
%token <ival> INT_LIT   "integer literal"
%token <fval> FLOAT_LIT "float literal"
%token <ival> BOOL_LIT  "boolean literal"

%token OP_PLUS   "+"
%token OP_MINUS  "-"
%token OP_MUL    "*"
%token OP_DIV    "/"
%token OP_MOD    "%"
%token OP_ASSIGN "="
%token OP_EQ     "=="
%token OP_NE     "!="
%token OP_LT     "<"
%token OP_LE     "<="
%token OP_GT     ">"
%token OP_GE     ">="
%token OP_AND    "&&"
%token OP_OR     "||"
%token OP_NOT    "!"

%token LPAREN "("
%token RPAREN ")"
%token LBRACE "{"
%token RBRACE "}"
%token SEMI   ";"
%token COMMA  ","

/* ---------------- Nonterminal value types ---------------- */
%type <node> decl var_decl func_decl param block stmt
%type <node> assign opt_assign opt_expr expr
%type <list> decl_list params param_list stmt_list args arg_list
%type <type> type

/* ---------------- Precedence and associativity ----------------
   Listed from LOWEST to HIGHEST precedence. Tokens on the same line have
   the same precedence. %left makes a - b - c mean (a - b) - c.

   Dangling else: in "if (a) if (b) s1 else s2", after s1 the parser can
   either reduce the inner if (without else) or shift "else". The rule
   without else gets %prec LOWER_THAN_ELSE, which is lower than "else", so
   Bison shifts: the else belongs to the nearest if, as in C.

   %precedence gives a token a precedence level but no associativity. It is
   used where associativity can never matter (Bison warns otherwise): the
   dangling-else markers and the prefix operators. */
%precedence LOWER_THAN_ELSE
%precedence "else"

%left  "||"
%left  "&&"
%left  "==" "!="
%left  "<" "<=" ">" ">="
%left  "+" "-"
%left  "*" "/" "%"
%precedence "!" UMINUS        /* unary operators bind tightest */

/* When the parser throws away symbols during error recovery, these free
   the memory they own. */
%destructor { free($$); }      <sval>
%destructor { ast_free($$); }  <node>
%destructor { list_free($$); } <list>

%start program

%%

/* ================================================================
 * Program structure
 * ================================================================ */

program
    : decl_list                     { ast_root = ast_program($1); }
    ;

decl_list
    : %empty                        { $$ = list_new(); }
    | decl_list decl                { $$ = list_append($1, $2); }
    ;

decl
    : var_decl
    | func_decl
    /* Recovery: skip a bad global declaration up to ';' or '}'. */
    | error ";"                     { $$ = NULL; }
    | error "}"                     { $$ = NULL; }
    ;

/* ================================================================
 * Declarations
 * ================================================================ */

var_decl
    : type IDENT ";"                { $$ = ast_var_decl(POS(@1), $1, $2, NULL); }
    | type IDENT "=" expr ";"       { $$ = ast_var_decl(POS(@1), $1, $2, $4); }
    ;

type
    : "int"                         { $$ = TYPE_INT; }
    | "float"                       { $$ = TYPE_FLOAT; }
    | "bool"                        { $$ = TYPE_BOOL; }
    ;

/* "void" is written out separately instead of being part of 'type'.
   Otherwise, after reading "int", the parser could not decide whether to
   reduce it to a variable type or a return type (a reduce/reduce conflict). */
func_decl
    : type IDENT "(" params ")" block
                                    { $$ = ast_func_decl(POS(@1), $1, $2, $4, $6); }
    | "void" IDENT "(" params ")" block
                                    { $$ = ast_func_decl(POS(@1), TYPE_VOID, $2, $4, $6); }
    /* Recovery: a bad parameter list. Keep the function, without params. */
    | type IDENT "(" error ")" block
                                    { $$ = ast_func_decl(POS(@1), $1, $2, list_new(), $6); }
    | "void" IDENT "(" error ")" block
                                    { $$ = ast_func_decl(POS(@1), TYPE_VOID, $2, list_new(), $6); }
    ;

params
    : %empty                        { $$ = list_new(); }
    | param_list
    ;

param_list
    : param                         { $$ = list_append(list_new(), $1); }
    | param_list "," param          { $$ = list_append($1, $3); }
    ;

param
    : type IDENT                    { $$ = ast_param(POS(@1), $1, $2); }
    ;

/* ================================================================
 * Statements
 * ================================================================ */

block
    : "{" stmt_list "}"             { $$ = ast_block(POS(@1), $2); }
    /* Recovery: an error just before the closing brace, e.g. "{ x = 1 }". */
    | "{" stmt_list error "}"       { $$ = ast_block(POS(@1), $2); }
    ;

stmt_list
    : %empty                        { $$ = list_new(); }
    | stmt_list stmt                { $$ = list_append($1, $2); }
    ;

stmt
    : var_decl                      /* local variable declaration */
    | assign ";"                    { $$ = $1; }
    | expr ";"                      { $$ = ast_expr_stmt(POS(@1), $1); }
    | "if" "(" expr ")" stmt %prec LOWER_THAN_ELSE
                                    { $$ = ast_if(POS(@1), $3, $5, NULL); }
    | "if" "(" expr ")" stmt "else" stmt
                                    { $$ = ast_if(POS(@1), $3, $5, $7); }
    | "while" "(" expr ")" stmt     { $$ = ast_while(POS(@1), $3, $5); }
    | "for" "(" opt_assign ";" opt_expr ";" opt_assign ")" stmt
                                    { $$ = ast_for(POS(@1), $3, $5, $7, $9); }
    | "return" ";"                  { $$ = ast_return(POS(@1), NULL); }
    | "return" expr ";"             { $$ = ast_return(POS(@1), $2); }
    | "print" "(" expr ")" ";"      { $$ = ast_print(POS(@1), $3); }
    | block

    /* ---- Error recovery ---- */
    /* Skip a bad statement up to the next ';'. */
    | error ";"                     { $$ = NULL; }
    /* A bad condition: skip to ')' and still parse the body, so errors
       inside the body are found too. The statement itself is dropped. */
    | "if" "(" error ")" stmt %prec LOWER_THAN_ELSE
                                    { ast_free($5); $$ = NULL; }
    | "if" "(" error ")" stmt "else" stmt
                                    { ast_free($5); ast_free($7); $$ = NULL; }
    | "while" "(" error ")" stmt    { ast_free($5); $$ = NULL; }
    ;

assign
    : IDENT "=" expr                { $$ = ast_assign(POS(@1), $1, $3); }
    ;

/* The three parts of a for loop are optional: for (;;) is allowed. */
opt_assign
    : %empty                        { $$ = NULL; }
    | assign
    ;

opt_expr
    : %empty                        { $$ = NULL; }
    | expr
    ;

/* ================================================================
 * Expressions
 *
 * The grammar is ambiguous on purpose ("expr + expr"); the precedence
 * declarations above tell Bison how to group operators. A binary node's
 * position is the position of its operator (@2).
 * ================================================================ */

expr
    : expr "||" expr                { $$ = ast_binary(POS(@2), BIN_OR,  $1, $3); }
    | expr "&&" expr                { $$ = ast_binary(POS(@2), BIN_AND, $1, $3); }
    | expr "==" expr                { $$ = ast_binary(POS(@2), BIN_EQ,  $1, $3); }
    | expr "!=" expr                { $$ = ast_binary(POS(@2), BIN_NE,  $1, $3); }
    | expr "<"  expr                { $$ = ast_binary(POS(@2), BIN_LT,  $1, $3); }
    | expr "<=" expr                { $$ = ast_binary(POS(@2), BIN_LE,  $1, $3); }
    | expr ">"  expr                { $$ = ast_binary(POS(@2), BIN_GT,  $1, $3); }
    | expr ">=" expr                { $$ = ast_binary(POS(@2), BIN_GE,  $1, $3); }
    | expr "+"  expr                { $$ = ast_binary(POS(@2), BIN_ADD, $1, $3); }
    | expr "-"  expr                { $$ = ast_binary(POS(@2), BIN_SUB, $1, $3); }
    | expr "*"  expr                { $$ = ast_binary(POS(@2), BIN_MUL, $1, $3); }
    | expr "/"  expr                { $$ = ast_binary(POS(@2), BIN_DIV, $1, $3); }
    | expr "%"  expr                { $$ = ast_binary(POS(@2), BIN_MOD, $1, $3); }
    /* %prec UMINUS: unary minus binds tighter than binary minus/times. */
    | "-" expr %prec UMINUS         { $$ = ast_unary(POS(@1), UN_NEG, $2); }
    | "!" expr                      { $$ = ast_unary(POS(@1), UN_NOT, $2); }
    | "(" expr ")"                  { $$ = $2; }
    | INT_LIT                       { $$ = ast_int(POS(@1), $1); }
    | FLOAT_LIT                     { $$ = ast_float(POS(@1), $1); }
    | BOOL_LIT                      { $$ = ast_bool(POS(@1), $1); }
    | IDENT                         { $$ = ast_var(POS(@1), $1); }
    | IDENT "(" args ")"            { $$ = ast_call(POS(@1), $1, $3); }
    ;

args
    : %empty                        { $$ = list_new(); }
    | arg_list
    ;

arg_list
    : expr                          { $$ = list_append(list_new(), $1); }
    | arg_list "," expr             { $$ = list_append($1, $3); }
    ;

%%

/* ================================================================
 * Syntax error messages
 *
 * With "%define parse.error custom", Bison calls yyreport_syntax_error()
 * on every syntax error it reports. We build a message such as
 *
 *   syntax error (line 6, col 9): unexpected '=', expecting identifier
 *   syntax error (line 11, col 11): unexpected integer literal '6', expecting ';'
 * ================================================================ */

/*
 * Prints a token's name for an error message. Bison stores alias names with
 * their double quotes ("\"int\""), so we strip those. Keywords and
 * punctuation are put in single quotes ('int', ';'). Descriptive names
 * ("identifier", "end of file") are printed as they are.
 */
static void print_symbol(yysymbol_kind_t sym)
{
    const char *name = yysymbol_name(sym);
    size_t len = strlen(name);

    if (len >= 2 && name[0] == '"' && name[len - 1] == '"') {
        name++;
        len -= 2;
    }

    int descriptive = (strncmp(name, "identifier", len) == 0 && len == 10)
                      || memchr(name, ' ', len) != NULL;   /* "end of file" */

    if (descriptive) {
        fprintf(stderr, "%.*s", (int)len, name);
    } else {
        fprintf(stderr, "'%.*s'", (int)len, name);
    }
}

/* Is this a closing delimiter: ';', ')' or '}'? */
static int is_closer(yysymbol_kind_t sym)
{
    return sym == YYSYMBOL_SEMI || sym == YYSYMBOL_RPAREN
        || sym == YYSYMBOL_RBRACE;
}

static int yyreport_syntax_error(const yypcontext_t *ctx)
{
    /* At most this many expected tokens are listed in a message. */
    enum { MAX_EXPECTED = 4 };
    yysymbol_kind_t expected[YYNTOKENS];

    yysymbol_kind_t lookahead = yypcontext_token(ctx);
    const YYLTYPE *loc = yypcontext_location(ctx);
    int n = yypcontext_expected_tokens(ctx, expected, YYNTOKENS);

    /*
     * When many tokens could come next, a full list is not helpful. After
     * "return n * n", for example, any operator or ';' may follow. Then we
     * list only the closing delimiters (';', ')', '}'), because a missing
     * one of those is by far the most likely mistake:
     *     unexpected '}', expecting ';'
     * If there is none, the "expecting" part is left out.
     */
    if (n > MAX_EXPECTED) {
        int kept = 0;
        for (int i = 0; i < n; i++) {
            if (is_closer(expected[i])) {
                expected[kept++] = expected[i];
            }
        }
        n = kept;
    }

    fflush(stdout);   /* keep stdout/stderr in order, as in lex_error() */
    fprintf(stderr, "syntax error (line %d, col %d): ",
            loc->first_line, loc->first_column);

    if (lookahead != YYSYMBOL_YYEMPTY) {
        fprintf(stderr, "unexpected ");
        print_symbol(lookahead);

        /* For identifiers and literals, also show the actual text. */
        if (lookahead == YYSYMBOL_IDENT || lookahead == YYSYMBOL_INT_LIT
            || lookahead == YYSYMBOL_FLOAT_LIT || lookahead == YYSYMBOL_BOOL_LIT) {
            fprintf(stderr, " '%s'", yytext);
        }
    }

    for (int i = 0; i < n; i++) {
        fprintf(stderr, i == 0 ? ", expecting " : " or ");
        print_symbol(expected[i]);
    }
    fprintf(stderr, "\n");

    syntax_error_count++;
    return 0;
}

/* Bison still calls yyerror() for other problems, e.g. "memory exhausted". */
void yyerror(const char *msg)
{
    fflush(stdout);
    fprintf(stderr, "syntax error (line %d, col %d): %s\n",
            yylloc.first_line, yylloc.first_column, msg);
    syntax_error_count++;
}
