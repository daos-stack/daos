#!/bin/bash
# Deploys the shared DAOS dev-environment setup files (setup-c.sh,
# setup-python.sh, setup-golang.sh, setup-dev.sh, envrc) into a target
# directory -- typically a per-ticket `git worktree` checkout under
# ~/work/tickets/daos-jira/DAOS-<N>/daos, or a PR-review `git clone
# --reference` checkout under ~/work/tickets/daos-jira/DAOS-<N>/repo.
#
# The 4 setup-*.sh scripts are symlinked back to this script's own
# directory (single source of truth: fix or improve them here, in
# ~/work/daos-tools, and every deployment target picks up the change
# immediately via its symlink -- no re-copying needed anywhere). This is
# safe because each script resolves its own directory via
# realpath "$(dirname "${BASH_SOURCE[0]}")", which resolves the invocation
# directory (a relative '.' against cwd), never the symlink's own target
# -- see setup-dev.sh's own header comment for the same reasoning.
#
# `envrc` is copied (not symlinked) to <target-dir>/.envrc -- deliberately
# an ordinary, standalone per-directory file, consistent with direnv
# convention, so one specific ticket can add a local override later
# without affecting any other worktree.
#
# Idempotent: safe to re-run against the same target directory -- existing
# symlinks/the existing .envrc are replaced, never left stale or errored on.
#
# Usage: deploy-daos-env.sh <target-dir>

set -euo pipefail

SCRIPT_DIR="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"

if [[ $# -ne 1 ]]; then
echo "deploy-daos-env.sh: [ERROR] usage: deploy-daos-env.sh <target-dir>" >&2
exit 1
fi

if [[ ! -d "$1" ]]; then
echo "deploy-daos-env.sh: [ERROR] target directory not found: $1" >&2
exit 1
fi

TARGET_DIR="$(realpath "$1")"

if [[ "$TARGET_DIR" == "$SCRIPT_DIR" ]]; then
echo "deploy-daos-env.sh: [ERROR] target directory is this script's own directory ($SCRIPT_DIR) -- refusing to self-link" >&2
exit 1
fi

SCRIPTS_TO_LINK=(setup-c.sh setup-python.sh setup-golang.sh setup-dev.sh)

for f in "${SCRIPTS_TO_LINK[@]}"; do
src="$SCRIPT_DIR/$f"
if [[ ! -f "$src" ]]; then
echo "deploy-daos-env.sh: [ERROR] missing source file: $src" >&2
exit 1
fi
dest="$TARGET_DIR/$f"
if [[ -e "$dest" && ! -L "$dest" ]]; then
echo "deploy-daos-env.sh: [INFO] replacing existing plain file with a symlink: $dest"
fi
ln -sf "$src" "$dest"
echo "deploy-daos-env.sh: [INFO] linked $dest -> $src"
done

ENVRC_SRC="$SCRIPT_DIR/envrc"
if [[ ! -f "$ENVRC_SRC" ]]; then
echo "deploy-daos-env.sh: [ERROR] missing source file: $ENVRC_SRC" >&2
exit 1
fi
ENVRC_DEST="$TARGET_DIR/.envrc"
cp "$ENVRC_SRC" "$ENVRC_DEST"
echo "deploy-daos-env.sh: [INFO] copied $ENVRC_DEST (from $ENVRC_SRC)"

echo ""
echo "deploy-daos-env.sh: [INFO] ready -- run: cd $TARGET_DIR && direnv allow"
