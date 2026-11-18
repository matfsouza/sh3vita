#!/bin/bash
# Commit only with a green suite (docs/REGRAS.md R-010): bash tools/commit_checked.sh <git commit arguments>
# Runs the whole pytest suite first; a red suite refuses the commit and shows the failures.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
source "$root/tools/env_vita.sh"
log="$root/.superpowers/pytest_commit.log"
mkdir -p "$root/.superpowers"
if ! "$root/.venv/Scripts/python" -m pytest "$root/tests" -q > "$log" 2>&1; then
    grep -E "^FAILED|^ERROR" "$log" || tail -5 "$log"
    echo "suite vermelha: commit recusado (R-010)" >&2
    exit 1
fi
tail -1 "$log"
git -C "$root" commit "$@"
