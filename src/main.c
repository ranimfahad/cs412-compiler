/*
 * main.c -- Command-line driver for the MiniLang compiler.
 *
 * Usage:  minilang [options] <file.mini>
 *
 *   --tokens     print the token stream (Phase 1)
 *   --ast        print the abstract syntax tree (Phase 2)
 *   --symbols    print the symbol table (Phase 3)
 *   --tac        print the three-address code (Phase 4)
 *   --optimized  print the optimized three-address code (Phase 5)
 *
 *   --stop-after=PHASE   stop after a phase: lex, parse, semantic, tac, opt
 *                        (the tests use this to check one phase at a time)
 *   --passes=LIST        optimization passes to run (Phase 5), e.g.
 *                        "fold,dce"; or "all" (default) or "none"
 *   --run                run the final code with the TAC interpreter
 *                        (to check that optimization keeps the meaning)
 *
 * With no options, the compiler runs every phase and only reports errors
 * and a summary.
 *
 * A phase runs only if the earlier phases found no errors: there is no
 * point type-checking a program that did not parse, or generating code for
 * one that has type errors. (Lexical errors do not stop the parser, so the
 * parser can still report syntax errors in the rest of the file.)
 *
 * Exit status: 0 = success, 1 = errors in the program, 2 = usage/file error.
 *
 * All five phases are implemented.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "tokens.h"
#include "lexer.h"
#include "symtab.h"
#include "semantic.h"
#include "tac.h"
#include "irgen.h"
#include "optimize.h"
#include "interp.h"

/* Compiler phases, in the order they run. */
enum phase {
    PHASE_LEX = 1,
    PHASE_PARSE,
    PHASE_SEMANTIC,
    PHASE_TAC,
    PHASE_OPT,
    PHASE_LAST = PHASE_OPT
};

/* Which phase outputs the user asked for. */
struct options {
    int tokens;
    int ast;
    int symbols;
    int tac;
    int optimized;
    int run;
    OptPasses passes;            /* which optimization passes to run */
    enum phase stop_after;       /* run phases up to and including this one */
    const char *filename;
};

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [options] <file.mini>\n"
            "\n"
            "Options:\n"
            "  --tokens     print the token stream (phase 1)\n"
            "  --ast        print the abstract syntax tree (phase 2)\n"
            "  --symbols    print the symbol table (phase 3)\n"
            "  --tac        print three-address code (phase 4)\n"
            "  --optimized  print optimized three-address code (phase 5)\n"
            "  --stop-after=PHASE\n"
            "               stop after PHASE: lex, parse, semantic, tac, opt\n"
            "  --passes=LIST\n"
            "               optimization passes: all (default), none, or a\n"
            "               comma-separated list of unroll,fold,strength,licm,dce\n"
            "  --run        run the final code with the TAC interpreter\n"
            "  --help       show this message\n",
            prog);
}

/* Parses argv into opts. Returns 0 on success, -1 on a usage error. */
static int parse_args(int argc, char **argv, struct options *opts)
{
    memset(opts, 0, sizeof *opts);
    opts->stop_after = PHASE_LAST;
    opt_parse_passes("all", &opts->passes);

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strncmp(arg, "--stop-after=", 13) == 0) {
            const char *phase = arg + 13;
            if (strcmp(phase, "lex") == 0)           opts->stop_after = PHASE_LEX;
            else if (strcmp(phase, "parse") == 0)    opts->stop_after = PHASE_PARSE;
            else if (strcmp(phase, "semantic") == 0) opts->stop_after = PHASE_SEMANTIC;
            else if (strcmp(phase, "tac") == 0)      opts->stop_after = PHASE_TAC;
            else if (strcmp(phase, "opt") == 0)      opts->stop_after = PHASE_OPT;
            else {
                fprintf(stderr, "error: unknown phase '%s' in %s\n", phase, arg);
                return -1;
            }
        }
        else if (strcmp(arg, "--tokens") == 0)    opts->tokens = 1;
        else if (strcmp(arg, "--ast") == 0)       opts->ast = 1;
        else if (strcmp(arg, "--symbols") == 0)   opts->symbols = 1;
        else if (strcmp(arg, "--tac") == 0)       opts->tac = 1;
        else if (strcmp(arg, "--optimized") == 0) opts->optimized = 1;
        else if (strcmp(arg, "--run") == 0)       opts->run = 1;
        else if (strncmp(arg, "--passes=", 9) == 0) {
            if (opt_parse_passes(arg + 9, &opts->passes) != 0) {
                fprintf(stderr, "error: unknown pass in %s\n", arg);
                return -1;
            }
        }
        else if (strcmp(arg, "--help") == 0)      return -1;
        else if (arg[0] == '-' && arg[1] == '-') {
            fprintf(stderr, "error: unknown option '%s'\n", arg);
            return -1;
        } else if (opts->filename == NULL) {
            opts->filename = arg;
        } else {
            fprintf(stderr, "error: only one input file is allowed\n");
            return -1;
        }
    }

    if (opts->filename == NULL) {
        fprintf(stderr, "error: no input file\n");
        return -1;
    }
    return 0;
}

/* Prints a section header such as "=== Tokens ===". */
static void section(const char *title)
{
    printf("=== %s ===\n", title);
}

/*
 * Phase 1 as a separate pass: runs the scanner over the whole input. With
 * print set (--tokens), each token is printed with its position.
 */
static void run_lexer(int print)
{
    int count = 0;
    int tok;

    if (print) {
        section("Tokens");
        printf("%-6s %-5s %-11s %s\n", "LINE", "COL", "TOKEN", "LEXEME");
        printf("%-6s %-5s %-11s %s\n", "----", "---", "-----", "------");
    }

    while ((tok = yylex()) != YYEOF) {
        count++;
        if (print) {
            printf("%-6d %-5d %-11s %s\n",
                   tok_line, tok_col, token_name(tok), yytext);
        }
        if (tok == IDENT) {
            free(yylval.sval);   /* the lexer copied the name for the parser */
        }
    }

    if (print) {
        printf("(%d token%s)\n\n", count, count == 1 ? "" : "s");
    }
}

/* Prints "N thing" or "N things". */
static void print_count(int n, const char *what)
{
    printf("%d %s%s", n, what, n == 1 ? "" : "s");
}

int main(int argc, char **argv)
{
    struct options opts;
    TacProgram *tac = NULL;

    if (parse_args(argc, argv, &opts) != 0) {
        usage(argv[0]);
        return 2;
    }

    yyin = fopen(opts.filename, "r");
    if (yyin == NULL) {
        perror(opts.filename);
        return 2;
    }

    /* ---- Phase 1: lexical analysis ----
       Normally the parser drives the lexer, asking for one token at a time.
       A separate pass over the input is made only to print the tokens
       (--tokens) or when stopping after this phase. */
    if (opts.tokens || opts.stop_after == PHASE_LEX) {
        run_lexer(opts.tokens);
        if (opts.stop_after > PHASE_LEX) {
            /* Scan again from the start for the parser. Lexical errors
               were already reported, so don't report them a second time. */
            lexer_reset(1);
        }
    }

    /* ---- Phase 2: syntax analysis ----
       yyparse() calls yylex() for each token it needs, so lexical errors
       are reported here when there was no separate lexer pass. */
    if (opts.stop_after >= PHASE_PARSE) {
        yyparse();

        if (opts.ast) {
            section("Abstract Syntax Tree");
            if (ast_root != NULL) {
                ast_print_tree(ast_root);
            } else {
                printf("(no AST: the parser could not recover before end of file)\n");
            }
            printf("\n");
        }
    }
    fclose(yyin);

    int front_end_ok = ast_root != NULL
                       && lex_error_count == 0 && syntax_error_count == 0;

    /* ---- Phase 3: semantic analysis ---- */
    symtab_init();
    if (opts.stop_after >= PHASE_SEMANTIC) {
        if (front_end_ok) {
            semantic_analyze(ast_root);
        }
        if (opts.symbols) {
            section("Symbol Table");
            if (front_end_ok) {
                symtab_print();
            } else {
                printf("(not built: the program has lexical or syntax errors)\n");
            }
            printf("\n");
        }
    }

    /* ---- Phase 4: three-address code ---- */
    if (opts.stop_after >= PHASE_TAC) {
        int ok = front_end_ok && semantic_error_count == 0;
        if (ok) {
            tac = irgen_program(ast_root);
        }
        if (opts.tac) {
            section("Three-Address Code");
            if (ok) {
                tac_print_program(tac);
            } else {
                printf("(not generated: the program has errors)\n");
            }
            printf("\n");
        }
    }

    /* ---- Phase 5: optimization ----
       The code printed by --tac above is the code BEFORE optimization;
       the passes then change it in place. */
    if (opts.stop_after >= PHASE_OPT) {
        if (tac != NULL) {
            if (opts.optimized) {
                section("Optimization Report");
            }
            optimize_program(tac, &opts.passes, opts.optimized);
            if (opts.optimized) {
                printf("\n");
                section("Optimized Three-Address Code");
                tac_print_program(tac);
                printf("\n");
            }
        } else if (opts.optimized) {
            section("Optimization Report");
            printf("(not optimized: the program has errors)\n\n");
        }
    }

    /* ---- --run: execute the final code ---- */
    int runtime_failed = 0;
    if (opts.run) {
        section("Program Output");
        if (tac != NULL) {
            runtime_failed = interp_run(tac);
        } else {
            printf("(not run: the program has errors)\n");
        }
        printf("\n");
    }

    /* ---- Summary: error counts of the phases that ran ---- */
    printf("Summary: ");
    print_count(lex_error_count, "lexical error");
    if (opts.stop_after >= PHASE_PARSE) {
        printf(", ");
        print_count(syntax_error_count, "syntax error");
    }
    if (opts.stop_after >= PHASE_SEMANTIC) {
        printf(", ");
        print_count(semantic_error_count, "semantic error");
    }
    printf("\n");

    /* The TAC points to names owned by the symbol table, so free it first. */
    tac_program_free(tac);
    symtab_free();
    ast_free(ast_root);

    int errors = lex_error_count + syntax_error_count + semantic_error_count;
    return (errors > 0 || runtime_failed) ? 1 : 0;
}
