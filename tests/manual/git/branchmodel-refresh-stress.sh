#!/bin/bash
# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

set -euo pipefail

if (( $# < 2 || $# > 3 )); then
    echo "Usage: $0 QT_CREATOR REPOSITORY [ITERATIONS]" >&2
    exit 2
fi

qtcreator=$1
repository=$2
iterations=${3:-200}
ref_prefix=refs/heads/qtc-branch-stress-$$-$RANDOM
slot_count=8

if [[ ! -x "$qtcreator" ]]; then
    echo "Qt Creator is not executable: $qtcreator" >&2
    exit 2
fi
if ! git -C "$repository" rev-parse --verify HEAD >/dev/null 2>&1; then
    echo "Not a Git repository with a commit: $repository" >&2
    exit 2
fi
if [[ ! "$iterations" =~ ^[1-9][0-9]*$ ]]; then
    echo "ITERATIONS must be a positive integer" >&2
    exit 2
fi
if [[ -n "$(git -C "$repository" for-each-ref --format='%(refname)' "$ref_prefix")" ]]; then
    echo "Ref namespace already exists: $ref_prefix" >&2
    exit 2
fi

test_output=$(mktemp)

cleanup()
{
    rm -f "$test_output"
    for ((slot = 0; slot < slot_count; ++slot)); do
        git -C "$repository" update-ref -d "$ref_prefix/$slot" >/dev/null 2>&1 || true
    done
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

export QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-offscreen}
export QTC_GIT_BRANCH_STRESS_REPOSITORY=$repository
export QTC_GIT_BRANCH_STRESS_ITERATIONS=$iterations
export QTC_GIT_BRANCH_STRESS_REF_PREFIX=$ref_prefix

"$qtcreator" -clientid "qtc-branch-stress-$$-$RANDOM" -tcs -test Git,BranchModelStressTest \
    2>&1 | tee "$test_output"
if ! grep -Fq 'PASS   : Git::Internal::BranchModelStressTest::testRefreshStress()' "$test_output"; then
    echo "The branch model stress test did not run successfully. Build with WITH_TESTS=ON." >&2
    exit 1
fi

echo "Completed $iterations branch create/delete refresh cycles."
