#!/bin/sh
#
# The errors suite. Every file here is supposed to FAIL to compile -- that
# is the point of it. What is being tested is the failure itself:
#
#   1. the build fails (a case that silently starts compiling is a bug),
#   2. it fails with tempo's own message, named by the "// EXPECT:" line at the
#      top of each file,
#   3. and it produces exactly ONE error.
#
# Rule 3 is the one that matters. A static_assert that fires correctly but is
# followed by six lines of "incomplete type" and "no member named ReturnType"
# has not fixed anything -- the message the user needs is still buried. Keeping
# the count pinned at one is what stops that from creeping back in.
#
# LC_ALL=C because the check greps for "error:", and a compiler running under a
# localized environment says it in another language.

set -u

CXX=${CXX:-g++}
CXXFLAGS=${CXXFLAGS:--std=c++20 -O0 -pthread -I../..}
DEFINES=${DEFINES:-}

cd "$(dirname "$0")" || exit 1

passed=0
failed=0

for source in [0-9]*.cpp; do
    expected=$(sed -n 's|^// EXPECT: ||p' "$source")
    if [ -z "$expected" ]; then
        echo "  FAIL  $source -- no '// EXPECT:' line in the file"
        failed=$((failed + 1))
        continue
    fi

    output=$(LC_ALL=C $CXX $CXXFLAGS $DEFINES -fsyntax-only "$source" 2>&1)
    status=$?

    if [ $status -eq 0 ]; then
        echo "  FAIL  $source -- compiled, but was expected to fail"
        failed=$((failed + 1))
        continue
    fi

    if ! printf '%s' "$output" | grep -qF "$expected"; then
        echo "  FAIL  $source -- expected message not found: \"$expected\""
        printf '%s\n' "$output" | sed -n '1,15p' | sed 's/^/        | /'
        failed=$((failed + 1))
        continue
    fi

    errors=$(printf '%s\n' "$output" | grep -c 'error:')
    if [ "$errors" -ne 1 ]; then
        echo "  FAIL  $source -- $errors errors, expected exactly 1"
        printf '%s\n' "$output" | grep 'error:' | sed 's/^/        | /'
        failed=$((failed + 1))
        continue
    fi

    echo "  ok    $source"
    passed=$((passed + 1))
done

echo ""
if [ $failed -eq 0 ]; then
    echo "PASS: $passed errors, each failing with one readable message"
    exit 0
fi
echo "FAIL: $failed of $((passed + failed)) errors"
exit 1
