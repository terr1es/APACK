#!/bin/sh
# Packs each fixture, runs what came out, and compares.
#
# The packager had no tests until this file existed: every claim about it was checked by
# hand, once, by whoever had just changed it. Two defects shipped that way -- a launcher that
# dropped its arguments, and a packer that looked for the compiler at its own path.
#
# Run:  ./tests/run.sh
#       ACVM=/path/to/acvm ./tests/run.sh      # a build other than the one on PATH

set -e

ACVM="${ACVM:-acvm}"
HERE="$(cd "$(dirname "$0")" && pwd)"
APACK="$(cd "$HERE/.." && pwd)/apack.ax"
WORK="$(cd "$(mktemp -d)" && pwd)"
trap 'rm -rf "$WORK"' EXIT

failures=0

fail() {
    printf '  FAIL  %s\n' "$1"
    failures=$((failures + 1))
}

pass() {
    printf '  ok    %s\n' "$1"
}

# Packs `$1` into `$WORK/$2`, naming the launcher `$2`. Extra arguments are passed through.
pack() {
    name="$1"
    launcher="$2"
    out="$WORK/$2"
    shift 2
    "$ACVM" run "$APACK" -- "$HERE/fixtures/$name.ax" -o "$out" --name "$launcher" "$@" \
        >"$WORK/$2.pack.log" 2>&1
}

# --- a bundle runs ---------------------------------------------------------

if pack hello hello >/dev/null 2>&1; then
    if [ ! -f "$WORK/hello/hello.exe" ]; then
        fail "hello: no launcher was produced"
    elif [ ! -f "$WORK/hello/hello.acvm" ]; then
        fail "hello: no payload was produced"
    else
        actual="$("$WORK/hello/hello.exe" 2>&1 | tr -d '\r' || true)"
        expected="$(printf 'hello\nworld')"
        if [ "$actual" = "$expected" ]; then
            pass "a bundle runs and prints what the program prints"
        else
            fail "hello: printed '$actual'"
        fi
    fi
else
    fail "hello: the pack itself failed"
    sed 's/^/        /' "$WORK/hello.pack.log"
fi

# --- a bundle receives its arguments ---------------------------------------

if pack args args >/dev/null 2>&1; then
    actual="$("$WORK/args/args.exe" alpha beta 2>&1 | tr -d '\r' || true)"
    expected="$(printf 'count=2\narg=alpha\narg=beta')"
    if [ "$actual" = "$expected" ]; then
        pass "a bundle hands its arguments to the program"
    else
        fail "args: printed '$actual'"
    fi
else
    fail "args: the pack itself failed"
fi

# --- a failed pack leaves nothing ------------------------------------------

if pack broken broken >/dev/null 2>&1; then
    fail "broken: a program that does not compile was packed anyway"
else
    leftover="$(ls -A "$WORK/broken" 2>/dev/null || true)"
    if [ -z "$leftover" ]; then
        pass "a failed pack leaves no half-written bundle"
    else
        fail "broken: left behind $leftover"
    fi
fi

# --- a bundle refuses to be built from a runtime that is missing -----------

if pack hello orphan --runtime "$WORK/nowhere" >/dev/null 2>&1; then
    fail "orphan: packed against a runtime directory that does not exist"
else
    pass "a missing runtime is refused"
fi

printf '\n%d failure(s)\n' "$failures"
[ "$failures" -eq 0 ]
