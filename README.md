# MiniLang Compiler (CS412 Course Project)

A compiler for **MiniLang**, a small C-like language, written in C using
**Flex** (lexical analysis) and **Bison** (syntax analysis).

The compiler is built phase by phase:

| Phase | What it does                                  | Flag          | Status   |
|-------|-----------------------------------------------|---------------|----------|
| 1     | Lexical analysis (Flex)                       | `--tokens`    | **Done** |
| 2     | Syntax analysis + AST (Bison, LALR)           | `--ast`       | **Done** |
| 3     | Semantic analysis (symbol table, type checks) | `--symbols`   | **Done** |
| 4     | Intermediate code (three-address code)        | `--tac`       | **Done** |
| 5     | Optimization of the three-address code        | `--optimized` | **Done** |

There is also a small **interpreter** for the three-address code (`--run`).
It is not one of the phases. Its job is to test the optimizer: every
test program must print the same output with and without optimization.

---

## 1. The MiniLang language

MiniLang programs are made of global variable declarations and function
definitions. Execution starts at `main`.

```c
// Global variables
int limit = 5;
float scale = 2.5;

/* Recursive factorial */
int fact(int n) {
    if (n <= 1) {
        return 1;
    }
    return n * fact(n - 1);
}

void main() {
    int i;
    bool done = false;

    for (i = 0; i < limit; i = i + 1) {
        print(fact(i));
    }

    while (!done && i >= 0) {
        i = i - 1;
        if (i % 2 == 0) {
            print(i * scale);
        } else {
            done = i != 1;
        }
    }
}
```

### Features

| Feature              | Syntax                                                   |
|----------------------|----------------------------------------------------------|
| Types                | `int`, `float`, `bool` (`void` only as a function return type) |
| Variable declaration | `int x;`   `float y = 1.5;`  (global or anywhere in a block) |
| Assignment           | `x = x + 1;`                                             |
| Arithmetic           | `+  -  *  /  %`   (unary minus: `-x`)                    |
| Relational           | `==  !=  <  <=  >  >=`                                   |
| Logical              | `&&  \|\|  !`                                            |
| If / else            | `if (cond) stmt`   `if (cond) stmt else stmt`            |
| While loop           | `while (cond) stmt`                                      |
| For loop             | `for (i = 0; i < n; i = i + 1) stmt` (each part optional) |
| Functions            | `int add(int a, int b) { return a + b; }`                |
| Function call        | `add(1, 2)`, also as a statement: `report(x);`           |
| Print                | `print(expr);`                                           |
| Blocks               | `{ ... }` (can be nested)                                |
| Comments             | `// to end of line`   and   `/* block, may span lines */` |

Assignment is a statement, not an expression (`a = b = c` is not allowed),
and the parts of a `for` loop are assignments, not declarations.

Every program needs a `main` function without parameters. Global variable
initializers must be constants (literals and operators, e.g.
`int n = 2 * 3;`). A global without an initializer starts at zero.

---

## 2. Phase 1: Lexical analysis

The lexer is defined in [`src/lexer.l`](src/lexer.l). Flex turns it into a C
scanner (`build/lexer.c`). Each call to `yylex()` returns the next token. It
also records the token's line and column, and for identifiers and literals
it stores the name or value for the parser.

### Tokens

| Category    | Token names                                   | Regular expression / text                     |
|-------------|-----------------------------------------------|-----------------------------------------------|
| Keywords    | `KW_INT KW_FLOAT KW_BOOL KW_VOID KW_IF KW_ELSE KW_WHILE KW_FOR KW_RETURN KW_PRINT` | `int float bool void if else while for return print` |
| Boolean     | `BOOL_LIT`                                    | `true` \| `false`                             |
| Identifier  | `IDENT`                                       | `[A-Za-z_][A-Za-z0-9_]*`                      |
| Integer     | `INT_LIT`                                     | `[0-9]+`                                      |
| Float       | `FLOAT_LIT`                                   | `[0-9]+"."[0-9]+([eE][+-]?[0-9]+)?` \| `[0-9]+[eE][+-]?[0-9]+` |
| Arithmetic  | `OP_PLUS OP_MINUS OP_MUL OP_DIV OP_MOD`       | `+ - * / %`                                   |
| Assignment  | `OP_ASSIGN`                                   | `=`                                           |
| Relational  | `OP_EQ OP_NE OP_LT OP_LE OP_GT OP_GE`         | `== != < <= > >=`                             |
| Logical     | `OP_AND OP_OR OP_NOT`                         | `&& \|\| !`                                   |
| Delimiters  | `LPAREN RPAREN LBRACE RBRACE SEMI COMMA`      | `( ) { } ; ,`                                 |

Whitespace (spaces, tabs, `\n`, `\r`) and comments produce no tokens.

### How Flex picks a rule

1. **Longest match wins.** `<=` is one `OP_LE` token, not `<` then `=`.
   `integer` is an identifier, not the keyword `int` followed by `eger`.
2. **On a tie, the first rule wins.** `int` matches both the keyword rule and
   the identifier rule, so the keyword rule is listed first.

Block comments use a Flex *start condition* (`%x COMMENT`). After `/*` the
scanner switches to the `COMMENT` state, where only the comment rules are
active. It switches back at `*/`.

### Lexical errors

Each error is printed with its line and column. The lexer then **keeps
scanning**, so all errors in a file are reported in one run.

| Error                        | Example        | Message                                                  |
|------------------------------|----------------|----------------------------------------------------------|
| Invalid character            | `@`  `#`  `$`  | `invalid character '@'`                                  |
| Single `&` or `\|`           | `a & b`        | `invalid character '&' (did you mean '&&'?)`             |
| Malformed number             | `12abc`  `1.5f` | `malformed number '12abc'`                              |
| Float missing digits         | `3.`           | `malformed float '3.' (expected digits after '.')`       |
| Integer too large            | `99999999999`  | `integer literal '99999999999' is too large (max 2147483647)` |
| Unterminated block comment   | `/* ...` (EOF) | `unterminated block comment` (at the line where it began) |

The invalid text is skipped and produces no token. The exception is an
integer that is too large: it is still returned as an `INT_LIT` (with value
2147483647), so the parser does not report a second error for it.

### Sample output

Input `tests/lexer/valid_crlf.mini`:

```c
int x = 1;
if (x >= 1) {
    print(x); // CRLF line endings
}
```

```
$ ./minilang --tokens --stop-after=lex tests/lexer/valid_crlf.mini
=== Tokens ===
LINE   COL   TOKEN       LEXEME
----   ---   -----       ------
1      1     KW_INT      int
1      5     IDENT       x
1      7     OP_ASSIGN   =
1      9     INT_LIT     1
1      10    SEMI        ;
2      1     KW_IF       if
2      4     LPAREN      (
2      5     IDENT       x
2      7     OP_GE       >=
2      10    INT_LIT     1
2      11    RPAREN      )
2      13    LBRACE      {
3      5     KW_PRINT    print
3      10    LPAREN      (
3      11    IDENT       x
3      12    RPAREN      )
3      13    SEMI        ;
4      1     RBRACE      }
(18 tokens)

Summary: 0 lexical errors
```

With errors (from `tests/lexer/err_malformed_numbers.mini`):

```
$ ./minilang --tokens --stop-after=lex tests/lexer/err_malformed_numbers.mini
=== Tokens ===
LINE   COL   TOKEN       LEXEME
----   ---   -----       ------
3      1     KW_INT      int
3      5     IDENT       a
3      7     OP_ASSIGN   =
lexical error (line 3, col 9): malformed number '12abc'
3      14    SEMI        ;
4      1     KW_FLOAT    float
4      7     IDENT       b
4      9     OP_ASSIGN   =
lexical error (line 4, col 11): malformed float '3.' (expected digits after '.')
4      13    SEMI        ;
...
(31 tokens)

Summary: 6 lexical errors
```

Errors go to stderr and everything else to stdout. stdout is flushed before
each error, so the two appear in the right order when they go to the same
file or terminal.

---

## 3. Phase 2: Syntax analysis

The grammar is in [`src/parser.y`](src/parser.y). Bison generates an
**LALR(1)** parser from it, which is a bottom-up parser:

- It **shifts** tokens onto a stack.
- When the top of the stack matches the right side of a rule, it
  **reduces** them to the rule's left side and runs the rule's action.
- The actions build the AST (`src/ast.h`, `src/ast.c`).

The parser asks the lexer for tokens one at a time. The lexer passes each
token's value through `yylval` and its position through `yylloc`. In the
grammar actions these are available as `$1, $2, ...` and `@1, @2, ...`.

### Grammar

Terminals are in quotes or CAPITALS. `ε` is the empty string.

```
program     → decl_list

decl_list   → decl_list decl | ε
decl        → var_decl | func_decl

var_decl    → type IDENT ";"
            | type IDENT "=" expr ";"
type        → "int" | "float" | "bool"

func_decl   → type   IDENT "(" params ")" block
            | "void" IDENT "(" params ")" block
params      → param_list | ε
param_list  → param_list "," param | param
param       → type IDENT

block       → "{" stmt_list "}"
stmt_list   → stmt_list stmt | ε

stmt        → var_decl
            | assign ";"
            | expr ";"
            | "if" "(" expr ")" stmt
            | "if" "(" expr ")" stmt "else" stmt
            | "while" "(" expr ")" stmt
            | "for" "(" opt_assign ";" opt_expr ";" opt_assign ")" stmt
            | "return" ";"
            | "return" expr ";"
            | "print" "(" expr ")" ";"
            | block

assign      → IDENT "=" expr
opt_assign  → assign | ε
opt_expr    → expr | ε

expr        → expr "||" expr | expr "&&" expr
            | expr "==" expr | expr "!=" expr
            | expr "<"  expr | expr "<=" expr | expr ">" expr | expr ">=" expr
            | expr "+"  expr | expr "-"  expr
            | expr "*"  expr | expr "/"  expr | expr "%" expr
            | "-" expr | "!" expr
            | "(" expr ")"
            | INT_LIT | FLOAT_LIT | BOOL_LIT
            | IDENT
            | IDENT "(" args ")"
args        → arg_list | ε
arg_list    → arg_list "," expr | expr
```

The lists use **left recursion** (`decl_list → decl_list decl`). An LR
parser handles this in constant stack space, because it reduces after every
item.

### Precedence and associativity

The `expr` rules are ambiguous on their own. For example, `1 + 2 * 3` could
be grouped two ways. Bison resolves this with precedence declarations,
listed from lowest to highest:

| Level | Operators              | Associativity |
|-------|------------------------|---------------|
| 1     | `\|\|`                 | left          |
| 2     | `&&`                   | left          |
| 3     | `==  !=`               | left          |
| 4     | `<  <=  >  >=`         | left          |
| 5     | `+  -`                 | left          |
| 6     | `*  /  %`              | left          |
| 7     | `!`  unary `-`         | (prefix)      |

Unary minus uses the same token as binary minus. The rule
`"-" expr %prec UMINUS` gives it the higher precedence, so `-2 * 3` is
`(-2) * 3`.

### Conflicts

`parser.y` has `%expect 0`, so **Bison stops the build if the grammar has
any conflict.** The grammar currently builds with 145 states and no
conflicts.

**The dangling else.** In `if (a) if (b) s1 else s2`, after `s1` the parser
can either reduce the inner `if` or shift the `else`. That is a
shift/reduce conflict. The rule without `else` is marked
`%prec LOWER_THAN_ELSE`, which ranks below `"else"`, so Bison shifts. The
`else` therefore belongs to the nearest `if`, as in C.

If that `%prec` is removed, Bison refuses to build and shows an example of
the ambiguity:

```
error: shift/reduce conflicts: 2 found, 0 expected
warning: shift/reduce conflict on token "else" [-Wcounterexamples]
  Example: "if" "(" expr ")" "if" "(" expr ")" stmt . "else" stmt
  Shift derivation
    stmt
    `-> 29: "if" "(" expr ")" stmt
                              `-> 30: "if" "(" expr ")" stmt . "else" stmt
  Reduce derivation
    stmt
    `-> 30: "if" "(" expr ")" stmt                             "else" stmt
                              `-> 29: "if" "(" expr ")" stmt .
```

**`void` is not part of `type`.** If `type` included `void`, then after
`int` the parser could not tell whether to reduce it as a variable type or
as a return type. That is a reduce/reduce conflict. Writing `"void"`
separately in `func_decl` avoids it.

To see every state of the parser, run
`bison --report=all src/parser.y -o /tmp/p.c` and open `/tmp/p.output`.

### Syntax errors and recovery

Each syntax error is reported with the line and column of the token where
the parser got stuck:

```
syntax error (line 6, col 5): unexpected '=', expecting identifier
syntax error (line 11, col 11): unexpected integer literal '6', expecting ';'
```

If at most 4 tokens could come next, the message lists them all. If more
could (for example, after `return n * n` any operator may follow), it lists
only the closing delimiters `;` `)` `}`, because a missing delimiter is the
most likely mistake:

```
syntax error (line 4, col 1): unexpected '}', expecting ';'
syntax error (line 10, col 24): unexpected ';', expecting ')'
```

The messages are built in `yyreport_syntax_error()`
(`%define parse.error custom`).

After an error the parser **recovers and continues**, so every error in the
file is reported in one run. Recovery uses Bison's `error` token:

1. The parser pops states off its stack until it reaches one that can
   shift `error`.
2. It skips input tokens until it finds one that can follow `error` in that
   rule.
3. It reduces the rule and continues normally. No new error is reported
   until 3 tokens have been shifted successfully, so one mistake does not
   cause a flood of messages.

| Recovery rule                                  | What it skips                               |
|------------------------------------------------|---------------------------------------------|
| `stmt → error ";"`                             | a bad statement, up to the next `;`         |
| `block → "{" stmt_list error "}"`              | a bad last statement, e.g. `{ x = 1 }`      |
| `stmt → "if" "(" error ")" stmt` (also `while`) | a bad condition. The body is still checked. |
| `func_decl → type IDENT "(" error ")" block`   | a bad parameter list                        |
| `decl → error ";"` and `decl → error "}"`      | a bad global declaration                    |

A statement or declaration that had an error is left out of the AST.

**Limitations:**
- If the file ends while the parser is still recovering (for example, a
  missing `}` at the end), Bison cannot recover. The parse stops and no AST
  is produced.
- A missing `)` in an `if` or `while` condition makes the parser skip to
  the next `)` it finds, which may be far away.

### The AST

Each construct becomes a `Node` (`src/ast.h`) with a kind, a source
position, and fields for its parts. Lists (statements, parameters,
arguments) are `NodeList`s, which are growable arrays. Phase 3 will fill in
each expression's `type` field.

`--ast` prints the tree with two spaces of indentation per level. Children
that have a specific role are labeled (`cond:`, `then:`, `body:`, ...), and
statements show their line number.

### Sample output

Input:

```c
void main() {
    int r;
    r = 1 + 2 * 3;
    r = 10 - 4 - 3;
    if (r > 0) if (r > 5) print(1); else print(2);
}
```

```
$ ./minilang --ast example.mini
=== Abstract Syntax Tree ===
Program
  FuncDecl void main  [line 1]
    params: (none)
    body: Block  [line 1]
      VarDecl int r  [line 2]
      Assign r  [line 3]
        Binary +
          Int 1
          Binary *
            Int 2
            Int 3
      Assign r  [line 4]
        Binary -
          Binary -
            Int 10
            Int 4
          Int 3
      If  [line 5]
        cond: Binary >
          Var r
          Int 0
        then: If  [line 5]
          cond: Binary >
            Var r
            Int 5
          then: Print  [line 5]
            Int 1
          else: Print  [line 5]
            Int 2

Summary: 0 lexical errors, 0 syntax errors
```

Here `*` is grouped before `+`, `-` groups from the left, and the `else`
belongs to the inner `if`.

With errors (from `tests/parser/err_recovery.mini`):

```
$ ./minilang --ast tests/parser/err_recovery.mini
syntax error (line 6, col 5): unexpected '=', expecting identifier
syntax error (line 8, col 1): unexpected identifier 'x'
syntax error (line 10, col 19): unexpected ')', expecting identifier
syntax error (line 16, col 13): unexpected ')'
syntax error (line 19, col 20): unexpected ')'
syntax error (line 23, col 1): unexpected '}', expecting ';'
=== Abstract Syntax Tree ===
Program
  VarDecl int good1  [line 5]
    init: Int 1
  VarDecl float good2  [line 7]
    init: Float 2.0
  FuncDecl int add  [line 10]
  ...
  FuncDecl void after  [line 25]
    params: (none)
    body: Block  [line 25]
      Print  [line 26]
        Var good1

Summary: 0 lexical errors, 6 syntax errors
```

---

## 4. Phase 3: Semantic analysis

The semantic analyzer ([`src/semantic.c`](src/semantic.c)) walks the AST
and checks the rules a grammar cannot express: whether names are declared,
and whether types match. Along the way it records, in the AST:

- `Node.type`: the type of every expression;
- `Node.sym`: for every name, the declaration it refers to (scope
  resolution).

Code generation needs both.

### The symbol table and scopes

The symbol table ([`src/symtab.c`](src/symtab.c)) is a **stack of
scopes**:

| Level | Scope              | Contains                                            |
|-------|--------------------|-----------------------------------------------------|
| 0     | `global`           | global variables and all functions                  |
| 1     | `<function>`       | the parameters **and** the top-level locals of a function |
| 2+    | `<function>/block@<line>` | the variables of a nested `{ }` block         |

- Entering a function or block **pushes** a scope, and leaving it
  **pops** the scope.
- A lookup searches from the innermost scope outward. So a declaration
  in an inner scope **shadows** one with the same name further out.
- Declaring a name twice is an error only within the **same** scope.
- Parameters share a scope with the function's top-level locals. So, as
  in C, `int f(int a) { int a; }` is a redeclaration.

Each symbol also gets a **TAC name**, which is unique within its function.
A local that has the same name as a global, or as another local of the same
function, is renamed `x_1`, `x_2`, and so on. The three-address code can
then use plain names without mixing up different variables.

### Two passes

1. **Pass 1** enters every global name in source order: variables with
   their types, and functions with their parameter types. Duplicate global
   names are reported here. All globals are therefore visible in the whole
   program. A function can call itself, or a function defined later in the
   file.
2. **Pass 2** checks the global initializers and then each function body.
   Inside a function, statements are checked in order, so a local variable
   can only be used after its declaration.

### Type rules

| Construct                        | Rule                                                    |
|----------------------------------|---------------------------------------------------------|
| `+ - * /`                        | int or float operands. float if either is float, else int |
| `%`                              | int operands, int result                                |
| `< <= > >=`                      | int or float operands, bool result                      |
| `== !=`                          | two numbers or two bools, bool result                   |
| `&& \|\| !`                      | bool operands, bool result                              |
| unary `-`                        | int or float                                            |
| `if` / `while` / `for` condition | must be bool (an int is **not** accepted)               |
| `=`, initializer, argument, `return` | same type, or **int → float** (widening, as in C)   |

`float → int` (narrowing) is an error, and so is any mix of `bool` with a
number.

### Semantic errors

| Check                      | Example message                                             |
|----------------------------|-------------------------------------------------------------|
| Undeclared variable        | `undeclared variable 'b'`                                   |
| Undeclared function        | `undeclared function 'missing'`                             |
| Redeclaration              | `'x' is already declared in this scope (at line 22)`        |
| Assignment type            | `cannot assign a value of type float to int variable 'x'`   |
| Operator types             | `operator '+' cannot be applied to bool and int`            |
| Condition type             | `condition of 'while' must be bool, but it is float`        |
| Argument count             | `function 'add' expects 2 arguments, but 1 was given`       |
| Argument type              | `argument 1 of 'add' must be int, but it is float`          |
| Variable vs. function      | `'n' is a variable, not a function`, `'add' is a function, not a variable (missing parentheses?)` |
| Void value                 | `operator '+' cannot be applied to void and int`, `cannot print a void value` |
| Return type                | `function 'wrong_type' must return int, but this returns bool` |
| Return in void function    | `void function 'nothing' cannot return a value`             |
| Missing return value       | `function 'number' must return a value of type int`         |
| Missing return path        | `function 'maybe' does not return a value on every path`    |
| Global initializer         | `initializer of global variable 'b' must be a constant`     |
| No `main`                  | `the program has no 'main' function`                        |

**No error cascades.** An expression that has an error gets the type
`<error>`, and checks involving `<error>` are skipped silently. So
`i = (b + 1) * 2 - 3;` with a bool `b` gives one message, not three.

The "every path returns" check is simple. A statement returns if it is a
`return`, if it is a block containing one, or if it is an `if`/`else`
where both branches return. Loops are not analyzed.

Semantic analysis only runs if lexing and parsing found no errors, because
an incomplete AST would cause misleading messages.

### Sample output

For `examples/demo.mini` (shown in section 5):

```
$ ./minilang --symbols examples/demo.mini
=== Symbol Table ===
NAME         KIND       TYPE               SCOPE                    LEVEL  LINE  TAC NAME
----         ----       ----               -----                    -----  ----  --------
square       function   int(int)           global                   0      2     square
main         function   void()             global                   0      6     main
n            parameter  int                square                   1      2     n
i            variable   int                main                     1      7     i
(4 symbols)
```

With shadowing (from `tests/semantic/valid_scopes.mini`):

```
count        variable   int                global                   0      7     count
count        variable   int                main                     1      19    count_1
x            variable   int                main                     1      20    x
x            variable   float              main/block@21            2      22    x_1
x            variable   bool               main/block@21/block@24   3      25    x_2
```

With errors (from `tests/semantic/err_type_mismatch.mini`):

```
semantic error (line 8, col 5): cannot assign a value of type float to int variable 'x'
semantic error (line 11, col 11): operator '+' cannot be applied to bool and int
semantic error (line 19, col 9): condition of 'if' must be bool, but it is int
...
Summary: 0 lexical errors, 0 syntax errors, 14 semantic errors
```

---

## 5. Phase 4: Three-address code

The code generator ([`src/irgen.c`](src/irgen.c)) translates the checked
AST into **three-address code (TAC)** ([`src/tac.h`](src/tac.h)). Each
instruction has at most three addresses: one result and up to two inputs.

| Instruction           | Meaning                                      |
|-----------------------|----------------------------------------------|
| `x = a`               | copy                                         |
| `x = a op b`          | binary operator (`+ - * / % == != < <= > >=`) |
| `x = -a`, `x = !a`    | unary operator                               |
| `x = (float) a`       | int → float conversion                       |
| `L1:`                 | label (a jump target, not counted as an instruction) |
| `goto L1`             | jump                                         |
| `ifFalse a goto L1`   | jump if `a` is false                         |
| `if a goto L1`        | jump if `a` is true                          |
| `param a`             | pass an argument                             |
| `x = call f, n`       | call `f` with the last `n` params (`call f, n` if no result) |
| `return`, `return a`  | return from the function                     |
| `print a`             | print a value                                |

An operand is a variable, a **temporary** (`t1`, `t2`, …), or a constant.

### How constructs are translated

- **Expressions** are evaluated into new temporaries.
  `c = a + b * 4` becomes `t1 = b * 4` followed by `c = a + t1`.
  - The last operation writes the variable directly, so there is no
    extra `t2 = a + t1; c = t2`.
  - Constants and variables need no instruction.
- **Implicit widening** that Phase 3 allowed is made explicit:
  `float f = i;` becomes `f = (float) i`.
- **`&&` and `||` short-circuit**, as in C. The right operand is evaluated
  only if it can change the result:
  ```
  ok = x > 0 && check(x);        t1 = x > 0
                                 ifFalse t1 goto L1
                                 param x
                                 t1 = call check, 1
                               L1:
                                 ok = t1
  ```
- **Control flow** uses labels and jumps:
  ```
  if (c) A else B         while (c) body           for (init; c; update) body
      t = c               L1: t = c                    init
      ifFalse t goto L1       ifFalse t goto L2    L1: t = c
      A                       body                     ifFalse t goto L2
      goto L2                 goto L1                  body
  L1: B                   L2:                          update
  L2:                                                  goto L1
                                                   L2:
  ```
- **Calls** evaluate every argument first, then emit the `param`s and the
  `call`. Nested calls therefore never interleave their parameters.
- **Globals** are listed and initialized in a separate `init` block, which
  runs before `main`.
- A **void function** without a final `return` gets one added.

TAC is only generated for a program without any errors. Each function's
instruction count is printed, followed by the total. Phase 5 compares
these counts before and after each optimization.

### Sample output

For `examples/demo.mini`:

```c
int square(int n) {
    return n * n;
}

void main() {
    int i;
    for (i = 1; i <= 3; i = i + 1) {
        if (i % 2 == 0) print(square(i));
        else print(-i);
    }
}
```

```
$ ./minilang --tac examples/demo.mini
=== Three-Address Code ===
function square(n):
    t1 = n * n
    return t1
    (2 instructions)
end square

function main():
    i = 1
L1:
    t1 = i <= 3
    ifFalse t1 goto L2
    t2 = i % 2
    t3 = t2 == 0
    ifFalse t3 goto L3
    param i
    t4 = call square, 1
    print t4
    goto L4
L3:
    t5 = -i
    print t5
L4:
    i = i + 1
    goto L1
L2:
    return
    (15 instructions)
end main

Total: 17 instructions

Summary: 0 lexical errors, 0 syntax errors, 0 semantic errors
```

---

## 6. Phase 5: Optimization

The optimizer ([`src/optimize.c`](src/optimize.c)) improves the
three-address code in place. `--optimized` prints a report with the
instruction count **before and after each pass**, followed by the
optimized code.

### The passes, in order

| # | Pass | Flag name | What it does |
|---|------|-----------|--------------|
| 1 | Loop unrolling | `unroll` | Copies the body of a `for` loop with constant bounds, removing the test and jump |
| 2 | Constant folding | `fold` | Computes constant expressions, propagates constants and copies, decides constant branches |
| 3 | Strength reduction | `strength` | Replaces operations with cheaper ones (`x * 8` → `x << 3`) |
| 4 | Loop-invariant code motion | `licm` | Moves computations that give the same result on every iteration out of the loop |
| 5 | Dead code elimination | `dce` | Removes unreachable code, unused results, dead stores and useless jumps |
| 6 | Repeat until stable | | Runs 2, 3 and 5 again until the code stops changing |

**Why this order.**
- Unrolling comes first because it recognizes loops by the exact shape
  the code generator emits. Its copies of the body are full of constants
  for folding to simplify.
- Dead code elimination comes after the others and removes what they leave
  behind.
- The passes create work for each other. For example, DCE removes the dead
  side of a decided branch together with its label. That joins two basic
  blocks, so constants can propagate further, which may decide the next
  branch. So folding, strength reduction and DCE are repeated until nothing
  changes.

`--passes=LIST` runs only some passes, e.g. `--passes=fold,dce`
(`all` is the default, `none` turns optimization off). This lets you see
the effect of each pass on its own.

### 1. Loop unrolling

A `for` loop is unrolled when:
- it has the generated shape: `i = c0` before the loop, the test
  `i <rel> c1`, and the update `i = i ± c2`, all with constants;
- `i` is a local variable;
- the body doesn't change `i`.

The number of iterations is found by simulating `i`.

- **Full unrolling** (at most 16 iterations and 64 instructions): the loop
  disappears and the body is repeated once per iteration.
  ```
  for (i = 0; i < 4; i = i + 1)       i = 0
      sum = sum + i;                  sum = sum + i ; i = i + 1
                                      sum = sum + i ; i = i + 1   (4 copies)
                                      ...
  ```
  Folding then reduces this to `sum = 6`.
- **Partial unrolling** (more iterations): if the count is divisible by 4
  (or 2), the body is repeated 4 (or 2) times *inside* the loop, so the
  test and the jump run 4 times less often. Labels inside the body get new
  numbers in each copy.

Unrolling makes the code **longer** but makes it **run faster** (see
*Measuring the effect* below).

### 2. Constant folding (and propagation)

- **Folding:** `t1 = 2 * 3` → `t1 = 6`. `(float) 2` → `2.0`.
- **Constant propagation:** after `x = 6`, later uses of `x` in the same
  basic block become `6`.
- **Copy propagation:** after `c = x`, uses of `c` become `x`, until `c` or
  `x` changes.
- **Branches:** `ifFalse true goto L` is removed, and
  `ifFalse false goto L` becomes `goto L`.
- **What is forgotten, and when:**
  - Everything known is forgotten at a label, because control can arrive
    there from elsewhere.
  - After a call, everything involving a **global** is forgotten, because
    the called function may change globals.
  - Globals' initial values are never assumed inside functions.
- **Never folded:** integer division by zero. It must still fail when the
  program runs.

### 3. Strength reduction

| Before            | After      | Types |
|-------------------|------------|-------|
| `x * 8`, `8 * x`  | `x << 3`   | int (any power of two) |
| `x * 2.0`         | `x + x`    | float |
| `x * 1`, `x / 1`, `x + 0`, `x - 0` | `x` | int (float: `* 1.0`, `/ 1.0`, `- 0.0`) |
| `x * 0`           | `0`        | int |

The `<<` operator doesn't exist in MiniLang. Only the optimizer creates it.
For floats, `x + 0.0` is left alone, because `-0.0 + 0.0` is `0.0` rather
than `-0.0`.

### 4. Loop-invariant code motion

A loop is found as a backward `goto` to an earlier label. An instruction in
the loop is moved in front of it when:
- it is a pure computation (no call, no print) into a temporary that is
  written only once;
- each operand is a constant, or a variable or temporary that the loop
  never writes. A global doesn't count if the loop contains a call, since
  the call may change it;
- it is not a division or `%`, unless the divisor is a constant other than
  `0` or `-1`. Otherwise, moving `100 / k` out of a loop that runs zero
  times would add a division-by-zero error that the original program never
  has.

Inner loops are handled first, so an expression can move out of several
nested loops (`m * n` in `tests/optimizer/opt_licm.mini`).

### 5. Dead code elimination

- **Unreachable code** after `goto` or `return`, up to the next label.
- **Jumps to the next instruction**, e.g. `goto L2` right before `L2:`.
- **Labels no jump uses.** Labels aren't counted as instructions, but
  removing them joins basic blocks.
- **Unused results.** A temporary or local variable that is never read is
  removed. A call whose result is unused stays, without its result,
  because it may print.
- **Dead stores.** A value overwritten later in the same basic block
  before anything reads it.
- **Self-copies** (`x = x`).

Assignments to **globals** are always kept, because another function may
read them.

### Measuring the effect

The report counts instructions in the code (the *static* count). That
number doesn't always show the benefit:
- LICM moves code without removing it, so the static count stays the same.
- Unrolling makes the code longer.

`--run` therefore also prints how many instructions were **executed**:

| Test program | Static count | Executed (none → all) |
|--------------|--------------|------------------------|
| `examples/demo.mini`                  | 17 → 8  | 35 → 8 |
| `tests/optimizer/opt_licm.mini`       | 38 → 38 | 236 → 198 |
| `tests/optimizer/opt_loop_unrolling.mini` | 32 → 34 | 276 → 163 |
| `tests/optimizer/opt_strength_reduction.mini` | 25 → 19 | 25 → 19 |

### What is not optimized (limitations)

- Constant and copy propagation work within a basic block. There is no
  dataflow analysis across blocks.
- Dead stores are found within a basic block, and unused variables only
  if they are never read anywhere. There is no full liveness analysis. For
  example, `i = -2` after a fully unrolled loop may remain.
- Strength reduction is local. Induction-variable strength reduction
  (turning `i * 4` inside a loop into a running sum) is not implemented.
- Unrolling recognizes loops only by the exact code shape that a counted
  `for` loop compiles to. A `while` loop written the same way
  (`k = 0; while (k < 3) { ...; k = k + 1; }`) compiles to the same code,
  so it is unrolled too. Any other loop shape is left alone.

### Sample output

For `examples/demo.mini` (its TAC is shown in section 5):

```
$ ./minilang --optimized --run examples/demo.mini
=== Optimization Report ===
PASS                         BEFORE  AFTER  CHANGE   DETAILS
----                         ------  -----  ------   -------
loop unrolling                   17     34     +17   1 loop fully unrolled (3 iterations), 0 partially
constant folding                 34     34       0   2 folded, 4 constants + 0 copies propagated, 1 branch decided
strength reduction               34     34       0   0 operations made cheaper
loop-invariant code motion       34     34       0   0 instructions moved out of loops
dead code elimination            34     27      -7   7 instructions removed
repeat until stable              27      8     -19   3 more rounds: 9 folded, 15 propagated, 2 branches, 0 reduced, 18 removed
total                            17      8      -9

=== Optimized Three-Address Code ===
function square(n):
    t1 = n * n
    return t1
    (2 instructions)
end square

function main():
    print -1
    param 2
    t4 = call square, 1
    print t4
    print -3
    return
    (6 instructions)
end main

Total: 8 instructions

=== Program Output ===
-1
4
-3
(8 instructions executed)
```

How this happened:
1. The loop runs 3 times, so it was unrolled fully.
2. In each copy `i` is a known constant, so `i % 2 == 0` was folded and each
   `if` was decided.
3. DCE removed the side of each `if` that never runs.

Only the real call `square(2)` is left. Without optimization, the same
program executes 35 instructions.

### The TAC interpreter (`--run`)

[`src/interp.c`](src/interp.c) runs the code: first `init`, then `main`.
- Each call gets its own frame of variables and temporaries.
- Integer arithmetic wraps around on overflow, the same way folding
  computes it.
- Division by zero is a runtime error.

`make test` uses it to check that the optimizer keeps the meaning of
programs (see *Tests*).

---

## 7. Building and running

### Requirements

- `gcc`, `make`, `flex`, `bison` (`diff` is optional; `make test` uses it to show failures)
- **Linux:** `sudo apt install build-essential flex bison`
- **Windows:** [MSYS2](https://www.msys2.org/). In the *MSYS2 UCRT64* terminal, run
  `pacman -S mingw-w64-ucrt-x86_64-gcc make flex bison diffutils`. To use
  the tools from PowerShell, add `C:\msys64\ucrt64\bin` and
  `C:\msys64\usr\bin` to PATH.

### Commands

```sh
make          # build ./minilang (minilang.exe on Windows)
make test     # build and run all tests
make clean    # remove build files and test outputs
```

```sh
./minilang [options] program.mini

  --tokens     print the token stream          (phase 1)
  --ast        print the abstract syntax tree  (phase 2)
  --symbols    print the symbol table          (phase 3)
  --tac        print three-address code        (phase 4)
  --optimized  print optimized code            (phase 5)
  --stop-after=PHASE   stop after a phase: lex, parse, semantic, tac, opt
  --passes=LIST        optimization passes: all (default), none, or e.g. fold,dce
                       (names: unroll, fold, strength, licm, dce)
  --run                run the final code with the TAC interpreter
```

Flags can be combined, e.g. `--tac --optimized --run`. `--tac` shows the
code *before* optimization and `--optimized` shows it *after*. Without any
flags, the compiler runs every phase and only prints errors and a summary.
Each phase runs only if the earlier phases found no errors. The exception
is lexical errors: they don't stop the parser, so it can still report
syntax errors.

**Exit status:** `0` = success, `1` = errors found in the program (or a
runtime error with `--run`), `2` = bad command line or unreadable file.

---

## 8. Tests

```
tests/
  run_tests.sh                   test runner (used by `make test`)
  lexer/                         phase 1, run with --tokens --stop-after=lex
    valid_program.mini           a complete program
    valid_all_tokens.mini        every token type
    valid_numbers_idents.mini    int/float forms, identifiers vs keywords
    valid_comments.mini          all comment forms; positions after comments
    valid_crlf.mini              Windows (CRLF) line endings
    err_invalid_chars.mini       @ $ # ? ~ ^ " ' and single & |
    err_malformed_numbers.mini   12abc, 3., 1.5f, 1e, overflow
    err_unterminated_comment.mini
  parser/                        phase 2, run with --ast --stop-after=parse
    valid_program.mini           every statement kind, functions, globals
    valid_precedence.mini        precedence and associativity of operators
    valid_control_flow.mini      dangling else, else-if, loops, nested blocks
    err_missing_semicolon.mini   missing ';' and ')'
    err_bad_expressions.mini     3 + * 4, (2 + 3, 5 6, x = ;
    err_recovery.mini            errors in globals, params, conditions, before '}'
    err_unexpected_eof.mini      file ends inside a function (no AST)
    err_lexical_and_syntax.mini  lexical and syntax errors in one file
  semantic/                      phase 3, run with --symbols --stop-after=semantic
    valid_scopes.mini            globals, shadowing at 3 levels, recursion, forward calls
    valid_types.mini             int -> float widening, bool operators
    err_undeclared.mini          undeclared names, use before declaration, out of scope
    err_redeclared.mini          duplicate globals, functions, params, locals
    err_type_mismatch.mini       wrong operand/assignment/condition types, no cascades
    err_function_calls.mini      argument count/types, variable vs. function, void values
    err_returns.mini             return type, missing value, value in void, missing path
    err_program_rules.mini       non-constant global initializers, no main
  tac/                           phase 4, run with --tac --stop-after=tac
    tac_expressions.mini         temporaries, precedence, unary operators
    tac_control_flow.mini        if, if/else, while, for, for (;;)
    tac_functions.mini           params, nested calls, returns, void calls
    tac_short_circuit.mini       && and || with jumps
    tac_conversions.mini         (float) conversions for widening
    tac_scopes_globals.mini      init block, renamed shadowed variables
  optimizer/                     phase 5, run with --optimized --run
    opt_constant_folding.mini    folding, propagation, decided branch, globals not assumed
    opt_dead_code.mini           unreachable code, unused variable, dead store, if (false)
    opt_strength_reduction.mini  x*8 -> x<<3, x*1, x+0, x*0, float x*2.0
    opt_licm.mini                invariants moved out of one and two loops; division kept
    opt_loop_unrolling.mini      full (4 iterations, counting down), partial (32), not (variable bound)
    opt_safety.mini              no unrolling if the body changes i, call changes a global, 10/0 kept
    opt_combined.mini            all passes together
  run/                           the interpreter, run with --run --passes=none
    run_programs.mini            recursion, loops, int/float division, short-circuit, scopes
  */expected/*.out               expected output (stdout + stderr + exit code)
```

For each `.mini` file, the runner saves the compiler's output to
`actual/<name>.out` and compares it with `expected/<name>.out`. Line
endings are ignored, so the same expected files work on Windows and Linux.
`--stop-after` keeps each suite focused on its own phase. Otherwise the
lexer tests, which are not all complete programs, would also report
syntax errors.

**Equivalence check.** `make test` also runs every program in `tests/tac`,
`tests/optimizer`, `tests/run` and `examples/demo.mini` with the
interpreter several times:
- with no optimization;
- with **each pass on its own**;
- with all passes.

All runs must print exactly the same output. Only the "instructions
executed" count may differ. This is what shows the optimizations are
correct, not just smaller.

To add a test, create a `.mini` file and run `make expected` to generate
its expected output. **Check the generated file by hand** before keeping it.

---

## 9. Project layout

```
Makefile
src/
  main.c       command-line driver (flags, runs each phase)
  lexer.l      phase 1: Flex specification
  lexer.h      scanner interface (yylex, token position, errors)
  tokens.h     token codes (from Bison's parser.h) and token_name()
  tokens.c     token names for --tokens output
  parser.y     phase 2: Bison grammar, error recovery, error messages
  ast.h/.c     AST nodes, constructors, printer (--ast), free
  symtab.h/.c  phase 3: scoped symbol table, printer (--symbols)
  semantic.h/.c phase 3: scope resolution and type checking
  tac.h/.c     phase 4: three-address code instructions, printer (--tac)
  irgen.h/.c   phase 4: AST -> three-address code
  optimize.h/.c phase 5: the optimization passes and report (--optimized)
  interp.h/.c  TAC interpreter (--run), used to test the optimizer
build/         generated files (lexer.c, parser.c, parser.h, *.o)
examples/      demo.mini, demo_errors.mini, demo_semantic.mini
tests/
```
