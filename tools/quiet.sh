#!/bin/sh
# Runs a make target with its full output in build/logs/<target>.log and
# prints only a summary: the result lines of each check and, on failure,
# the failures and the end of the log. Keeps agent and CI transcripts short.
#
#   tools/quiet.sh test          # same as `make test`
#   tools/quiet.sh compare-stages
set -u
target=${1:-test}
shift 2>/dev/null || true
dir=build/logs
mkdir -p "$dir"
log="$dir/$target.log"
make "$target" "$@" >"$log" 2>&1
status=$?
# Result lines printed by the runners and tools.
grep -E '^[0-9]+ tests:|^FLAKY|^stages:|^verify:|^unit tests|^std modules|passed|^ok:|clean$' "$log" \
    | grep -v '^make' | tail -n 20
if [ "$status" -ne 0 ]; then
    echo "--- failures ($log) ---"
    grep -nE 'FAIL|error|Error|differ' "$log" | head -n 40
    echo "--- end of log ---"
    tail -n 25 "$log"
    echo "quiet: make $target failed (status $status); full log: $log"
else
    echo "quiet: make $target ok; full log: $log"
fi
exit "$status"
