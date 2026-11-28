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

# The launcher carries an executable suffix on Windows and none elsewhere. Hard-coding
# ".exe" here is how this file would pass on the machine it was written on and fail on
# every other one -- the same defect the packager itself had.
case "$(uname -s 2>/dev/null)" in
    MINGW*|MSYS*|CYGWIN*) EXE=".exe" ;;
    *) EXE="" ;;
esac
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
    if [ ! -f "$WORK/hello/hello$EXE" ]; then
        fail "hello: no launcher was produced"
    elif [ ! -f "$WORK/hello/hello.acvm" ]; then
        fail "hello: no payload was produced"
    else
        actual="$("$WORK/hello/hello$EXE" 2>&1 | tr -d '\r' || true)"
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
    actual="$("$WORK/args/args$EXE" alpha beta 2>&1 | tr -d '\r' || true)"
    expected="$(printf 'count=2\narg=alpha\narg=beta')"
    if [ "$actual" = "$expected" ]; then
        pass "a bundle hands its arguments to the program"
    else
        fail "args: printed '$actual'"
    fi
else
    fail "args: the pack itself failed"
fi

# --- a bundle carries its shared libraries ---------------------------------
#
# The only fixture that has any. Without it the library loop -- find, copy, verify, and the
# cleanup that names what was written -- was never executed by this file, and it contained the
# ".dll" literals that made the packager Windows-only.

if pack withlib withlib >/dev/null 2>&1; then
    missing=""
    for lib in os io; do
        found=""
        for candidate in "$WORK/withlib/$lib.dll" "$WORK/withlib/lib$lib.so" \
                         "$WORK/withlib/$lib.so" "$WORK/withlib/lib$lib.dylib" \
                         "$WORK/withlib/$lib.dylib"; do
            if [ -f "$candidate" ]; then found="$candidate"; fi
        done
        [ -n "$found" ] || missing="$missing $lib"
    done
    if [ -n "$missing" ]; then
        fail "withlib: no shared object for$missing"
    else
        pass "a bundle carries the shared objects it needs, under their own names"
    fi
    actual="$("$WORK/withlib/withlib$EXE" 2>&1 | tr -d '\r' || true)"
    if [ -n "$actual" ]; then
        pass "a bundle with libraries runs"
    else
        fail "withlib: the launcher printed nothing"
    fi
else
    fail "withlib: the pack itself failed"
    sed 's/^/        /' "$WORK/withlib.pack.log" 2>/dev/null || true
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

# --- nothing is written outside the scratch directory ----------------------
#
# `-o` is absolute and points into `$WORK`, so a correct pack leaves the source tree alone.
# One did not: a mangled `-o` value was taken as a relative path, and the packager created
# `Userstb918AppDataLocalTemp/tmp.gIH1mmepSO/...` here -- two 2.5 MB copies of the runtime,
# which went into the repository because nothing looked. This is the check that looks.
#
# The list is the whole source tree. A new file at the top level is either deliberate (and
# belongs in this list) or a pack that wrote where it was not told to.
stray="$(cd "$HERE/.." && ls -A | grep -v -E '^(\.git|\.gitattributes|\.gitignore|LICENSE|README\.md|apack\.ax|tests)$' || true)"
if [ -n "$stray" ]; then
    fail "the source tree gained: $(printf '%s' "$stray" | tr '\n' ' ')"
else
    pass "nothing was written outside the scratch directory"
fi

printf '\n%d failure(s)\n' "$failures"
[ "$failures" -eq 0 ]
