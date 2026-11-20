#!/bin/sh
# Lists the flaky tests of a CI job (build/logs/flaky-stage<N>.txt, written by
# tests/runner/run_tests.py) in the job summary. A flaky test failed once and
# then passed: it counts as passed and stays in the suite (ADR 0091).
set -u
summary=${GITHUB_STEP_SUMMARY:-/dev/stdout}
found=0
for list in build/logs/flaky-stage*.txt; do
    [ -s "$list" ] || continue
    if [ "$found" -eq 0 ]; then
        echo "### Flaky tests" >>"$summary"
        found=1
    fi
    while IFS= read -r test; do
        echo "- \`$test\` ($(basename "$list" .txt))" >>"$summary"
    done <"$list"
done
[ "$found" -eq 1 ] || echo "no flaky tests"
