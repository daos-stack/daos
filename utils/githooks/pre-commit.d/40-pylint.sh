#!/bin/sh

# Runs pylint for the DAOS project as a commit hook.
#
# To get the most out of this hook the 'gh' command should be installed and working.

set -ue

echo "Pylint:"
# shellcheck disable=SC1091
. utils/githooks/find_base.sh

if [ "$TARGET" = "HEAD" ]; then
    echo "  Checking against HEAD"
    git diff HEAD -U10 | ./utils/cq/daos_pylint.py --diff
else
    echo "  Checking against branch ${TARGET}"
    git diff "$TARGET"... -U10 | ./utils/cq/daos_pylint.py --diff
fi
