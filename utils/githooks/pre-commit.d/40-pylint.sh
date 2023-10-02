#!/bin/sh

# Runs pylint for the DAOS project as a commit hook.
#
# To get the most out of this hook the 'gh' command should be installed and working.

set -ue

echo "Pylint:"
# shellcheck disable=SC1091
. utils/githooks/find_base.sh


if [ -f utils/cq/daos_pylint.py ]; then
    if [ "$TARGET" = "HEAD" ]; then
            echo "  Checking against HEAD"
            git diff HEAD --name-only | ./utils/cq/daos_pylint.py --files-from-stdin
    else
            echo "  Checking against branch ${TARGET}"
            git diff "$TARGET"... --name-only | ./utils/cq/daos_pylint.py --files-from-stdin
    fi
fi
