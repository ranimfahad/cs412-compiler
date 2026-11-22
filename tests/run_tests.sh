#!/bin/sh
#
# run_tests.sh -- Runs every MiniLang test program and checks its output.
#
# Usage:  sh tests/run_tests.sh <compiler> [--update]
#
# Each test suite is a folder in tests/ containing *.mini programs. For a
# program tests/<suite>/foo.mini, the compiler is run with that suite's
# flags. Its stdout, stderr and exit code are captured together and compared
# with tests/<suite>/expected/foo.out.
#
# With --update, the expected files are rewritten from the current output
# instead (review them before keeping them).
#
# Line endings are ignored (\r is stripped), so the tests pass on Windows
# as well as Linux. Only standard tools are used (no diff required).

COMPILER=$1
UPDATE=$2

if [ -z "$COMPILER" ]; then
    echo "usage: sh tests/run_tests.sh <compiler> [--update]" >&2
    exit 2
fi

TESTS_DIR=$(dirname "$0")
pass=0
fail=0

# run_suite <folder> <compiler flags>
run_suite() {
    suite=$1
    flags=$2
    dir="$TESTS_DIR/$suite"

    [ -d "$dir" ] || return 0
    echo "== $suite ($flags) =="
    mkdir -p "$dir/actual" "$dir/expected"

    for prog in "$dir"/*.mini; do
        [ -f "$prog" ] || continue
        name=$(basename "$prog" .mini)
        actual="$dir/actual/$name.out"
        expected="$dir/expected/$name.out"

        # Run the compiler; record all output plus the exit code.
        # The file is passed as tests/<suite>/<name>.mini so that any paths
        # in the output are the same on every machine.
        "$COMPILER" $flags "$prog" > "$actual" 2>&1
        echo "[exit $?]" >> "$actual"

        # Remove carriage returns (Windows line endings).
        tr -d '\r' < "$actual" > "$actual.tmp" && mv "$actual.tmp" "$actual"

        if [ "$UPDATE" = "--update" ]; then
            cp "$actual" "$expected"
            echo "  updated  $name"
            continue
        fi

        if [ ! -f "$expected" ]; then
            echo "  MISSING  $name (no expected/$name.out)"
            fail=$((fail + 1))
        elif [ "$(cat "$actual")" = "$(tr -d '\r' < "$expected")" ]; then
            echo "  pass     $name"
            pass=$((pass + 1))
        else
            echo "  FAIL     $name"
            if command -v diff > /dev/null 2>&1; then
                tr -d '\r' < "$expected" | diff - "$actual" | sed 's/^/           /'
            else
                echo "           expected: $expected"
                echo "           actual:   $actual"
            fi
            fail=$((fail + 1))
        fi
    done
}

# One line per phase: test folder and the flags used for it.
# --stop-after keeps each suite focused on its own phase, so that, for
# example, a lexer test isn't affected by syntax errors.
run_suite lexer    "--tokens --stop-after=lex"
run_suite parser   "--ast --stop-after=parse"
run_suite semantic "--symbols --stop-after=semantic"
run_suite tac      "--tac --stop-after=tac"
run_suite optimizer "--optimized --run"
run_suite run      "--run --passes=none"

if [ "$UPDATE" = "--update" ]; then
    exit 0
fi

# ------------------------------------------------------------------
# Equivalence: optimization must not change what a program prints.
#
# Each program is run without optimization (--passes=none), with every
# pass on its own, and with all passes. All runs must give the same
# output. Only the "(N instructions executed)" line may differ, since
# making the program do less work is the point of optimizing.
# ------------------------------------------------------------------

# Program output (and errors) of one run, without the executed-count line.
run_output() {
    "$COMPILER" --run "--passes=$1" "$2" 2>&1 | tr -d '\r' | grep -v 'executed)$'
}

echo "== optimizer keeps the program output the same =="
for prog in "$TESTS_DIR"/tac/*.mini "$TESTS_DIR"/optimizer/*.mini \
            "$TESTS_DIR"/run/*.mini "$TESTS_DIR"/../examples/demo.mini; do
    [ -f "$prog" ] || continue
    name=$(basename "$prog" .mini)
    base=$(run_output none "$prog")
    differ=""
    for passes in unroll fold strength licm dce all; do
        if [ "$(run_output "$passes" "$prog")" != "$base" ]; then
            differ="$differ $passes"
        fi
    done
    if [ -z "$differ" ]; then
        echo "  pass     $name"
        pass=$((pass + 1))
    else
        echo "  FAIL     $name (output changes with:$differ)"
        fail=$((fail + 1))
    fi
done

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
