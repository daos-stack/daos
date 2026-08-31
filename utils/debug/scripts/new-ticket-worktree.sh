#!/bin/bash
# Creates (or re-syncs) a per-ticket DAOS git-worktree checkout for dev/fix
# work, deploys the shared env-setup files into it (see deploy-daos-env.sh),
# and optionally seeds it from an existing ticket's skeleton of
# build/deploy/test scripts. See README-worktrees.md for the full workflow.
#
# Usage:
#   new-ticket-worktree.sh --ticket DAOS-NNNNN [--type dev|fix] [--base NAME]
#                          [--patch NNN] [--skeleton-from DAOS-YYYYY] [--dry-run]
#   new-ticket-worktree.sh --list
#   new-ticket-worktree.sh --help
#
# On success, prints the final absolute worktree path as the LAST line of
# stdout -- scripts/agents/skills should rely on this, not on parsing the
# [INFO] lines above it.
#
# Env overrides:
#   DAOS_MAIN_REPO   Main DAOS checkout to branch/worktree from (default: ~/work/daos)
#   DAOS_TOOLS_DIR   Permanent ckochhof/dbg/master worktree (default: ~/work/daos-tools)
#   DAOS_TICKETS_DIR Base directory for per-ticket workspaces (default: ~/work/tickets/daos-jira)

set -euo pipefail

SCRIPT_DIR="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
DEPLOY_ENV="$SCRIPT_DIR/deploy-daos-env.sh"

DAOS_MAIN_REPO="${DAOS_MAIN_REPO:-$HOME/work/daos}"
DAOS_TOOLS_DIR="${DAOS_TOOLS_DIR:-$HOME/work/daos-tools}"
DAOS_TICKETS_DIR="${DAOS_TICKETS_DIR:-$HOME/work/tickets/daos-jira}"

TICKET=""
TYPE="dev"
BASE="master"
PATCH=""
SKELETON_FROM=""
DRY_RUN=0
DO_LIST=0

usage() {
cat <<'EOF'
Usage:
  new-ticket-worktree.sh --ticket DAOS-NNNNN [--type dev|fix] [--base NAME]
                         [--patch NNN] [--skeleton-from DAOS-YYYYY] [--dry-run]
  new-ticket-worktree.sh --list
  new-ticket-worktree.sh --help

Flags:
  --ticket DAOS-NNNNN     Ticket ID (required unless --list/--help)
  --type dev|fix          Branch namespace segment (default: dev)
  --base NAME             Base branch to fork from, e.g. master, release/2.8 (default: master)
  --patch NNN             Patch number (default: auto-incremented from existing
                          local+origin branches for this ticket/type/base, else 001)
  --skeleton-from TICKET  Seed the new ticket dir from an existing ticket's
                          build/deploy/test script skeleton (env.sh, install/
                          update-daos.sh, run-*.sh, check-*.sh, inventory.yml,
                          files/, setup/, tests/*.c). Rewrites DAOS_SRC/
                          daos_source_dir to the new worktree path and
                          DAOS_BUILD/daos_build_dir to a ticket-unique suffix.
                          Does NOT run ansible-playbook -- prints the command.
  --dry-run               Print planned actions without executing them
  --list                  List all ticket worktrees under DAOS_TICKETS_DIR
  --help                  Show this help

Env overrides:
  DAOS_MAIN_REPO   Main DAOS checkout to branch/worktree from (default: ~/work/daos)
  DAOS_TOOLS_DIR   Permanent ckochhof/dbg/master worktree (default: ~/work/daos-tools)
  DAOS_TICKETS_DIR Base directory for per-ticket workspaces (default: ~/work/tickets/daos-jira)
EOF
}

normalize_ticket() {
local t
t="$(echo "$1" | tr '[:lower:]' '[:upper:]')"
if [[ "$t" =~ ^[0-9]+$ ]]; then
t="DAOS-$t"
fi
echo "$t"
}

while [[ $# -gt 0 ]]; do
case "$1" in
--ticket) TICKET="$2"; shift 2 ;;
--type) TYPE="$2"; shift 2 ;;
--base) BASE="$2"; shift 2 ;;
--patch) PATCH="$2"; shift 2 ;;
--skeleton-from) SKELETON_FROM="$2"; shift 2 ;;
--dry-run) DRY_RUN=1; shift ;;
--list) DO_LIST=1; shift ;;
--help|-h) usage; exit 0 ;;
*) echo "new-ticket-worktree.sh: [ERROR] unknown argument: $1" >&2; usage >&2; exit 1 ;;
esac
done

if [[ "$DO_LIST" -eq 1 ]]; then
if [[ ! -d "$DAOS_TICKETS_DIR" ]]; then
echo "new-ticket-worktree.sh: [ERROR] DAOS_TICKETS_DIR not found: $DAOS_TICKETS_DIR" >&2
exit 1
fi
printf "%-14s %-7s %-50s %s\n" "TICKET" "STATE" "BRANCH" "PATH"
for dir in "$DAOS_TICKETS_DIR"/*/daos; do
[[ -d "$dir" ]] || continue
git -C "$dir" rev-parse --git-dir >/dev/null 2>&1 || continue
ticket="$(basename "$(dirname "$dir")")"
branch="$(git -C "$dir" rev-parse --abbrev-ref HEAD 2>/dev/null || echo "?")"
if [[ -n "$(git -C "$dir" status --porcelain 2>/dev/null)" ]]; then
state="dirty"
else
state="clean"
fi
printf "%-14s %-7s %-50s %s\n" "$ticket" "$state" "$branch" "$dir"
done
exit 0
fi

if [[ -z "$TICKET" ]]; then
echo "new-ticket-worktree.sh: [ERROR] --ticket is required (or use --list/--help)" >&2
exit 1
fi

TICKET="$(normalize_ticket "$TICKET")"
if [[ ! "$TICKET" =~ ^DAOS-[0-9]+$ ]]; then
echo "new-ticket-worktree.sh: [ERROR] invalid ticket ID: $TICKET (expected DAOS-NNNNN)" >&2
exit 1
fi
TICKET_LC="$(echo "$TICKET" | tr '[:upper:]' '[:lower:]')"

if [[ "$TYPE" != "dev" && "$TYPE" != "fix" ]]; then
echo "new-ticket-worktree.sh: [ERROR] --type must be dev or fix (got: $TYPE)" >&2
exit 1
fi

if [[ ! -d "$DAOS_MAIN_REPO" ]] || ! git -C "$DAOS_MAIN_REPO" rev-parse --git-dir >/dev/null 2>&1; then
echo "new-ticket-worktree.sh: [ERROR] DAOS_MAIN_REPO is not a git repository: $DAOS_MAIN_REPO" >&2
exit 1
fi

if [[ ! -x "$DEPLOY_ENV" ]]; then
echo "new-ticket-worktree.sh: [ERROR] deploy-daos-env.sh not found/executable at $DEPLOY_ENV" >&2
echo "  Set up the tools worktree first: git -C \"$DAOS_MAIN_REPO\" worktree add \"$DAOS_TOOLS_DIR\" ckochhof/dbg/master" >&2
exit 1
fi

BRANCH_PREFIX="ckochhof/$TYPE/$BASE/$TICKET_LC/patch-"

if [[ -z "$PATCH" ]]; then
max=0
while IFS= read -r ref; do
[[ -z "$ref" ]] && continue
num="${ref#"$BRANCH_PREFIX"}"
if [[ "$num" =~ ^[0-9]+$ ]]; then
num=$((10#$num))
[[ "$num" -gt "$max" ]] && max=$num
fi
done < <(
{
git -C "$DAOS_MAIN_REPO" for-each-ref --format='%(refname:short)' "refs/heads/${BRANCH_PREFIX}*"
git -C "$DAOS_MAIN_REPO" for-each-ref --format='%(refname:short)' "refs/remotes/origin/${BRANCH_PREFIX}*" | sed 's#^origin/##'
} | sort -u
)
PATCH="$(printf "%03d" $((max + 1)))"
fi

BRANCH="${BRANCH_PREFIX}${PATCH}"
TICKET_DIR="$DAOS_TICKETS_DIR/$TICKET"
WORKTREE_DIR="$TICKET_DIR/daos"

EXISTING_WORKTREE=0
if [[ -d "$WORKTREE_DIR" ]]; then
REAL_WORKTREE_DIR="$(realpath "$WORKTREE_DIR")"
if git -C "$DAOS_MAIN_REPO" worktree list --porcelain | grep -qxF "worktree $REAL_WORKTREE_DIR"; then
EXISTING_WORKTREE=1
else
echo "new-ticket-worktree.sh: [ERROR] $WORKTREE_DIR exists but is not a registered git worktree of $DAOS_MAIN_REPO -- refusing to touch it" >&2
exit 1
fi
fi

if [[ "$EXISTING_WORKTREE" -eq 1 ]]; then
ACTUAL_BRANCH="$(git -C "$WORKTREE_DIR" rev-parse --abbrev-ref HEAD)"
echo "new-ticket-worktree.sh: [INFO] ticket=$TICKET worktree=$WORKTREE_DIR branch=$ACTUAL_BRANCH"
echo "new-ticket-worktree.sh: [INFO] worktree already exists and is registered -- re-syncing env files only (the worktree's branch is a fixed path per ticket; switch branches inside it directly with plain git if you need a different patch)"
else
echo "new-ticket-worktree.sh: [INFO] ticket=$TICKET type=$TYPE base=$BASE patch=$PATCH branch=$BRANCH"
echo "new-ticket-worktree.sh: [INFO] worktree=$WORKTREE_DIR"
BRANCH_EXISTS_LOCAL=0
git -C "$DAOS_MAIN_REPO" show-ref --verify --quiet "refs/heads/$BRANCH" && BRANCH_EXISTS_LOCAL=1
BRANCH_EXISTS_REMOTE=0
git -C "$DAOS_MAIN_REPO" show-ref --verify --quiet "refs/remotes/origin/$BRANCH" && BRANCH_EXISTS_REMOTE=1

if [[ "$BRANCH_EXISTS_LOCAL" -eq 1 ]]; then
WORKTREE_ADD_ARGS=("$WORKTREE_DIR" "$BRANCH")
echo "new-ticket-worktree.sh: [INFO] reusing existing local branch $BRANCH"
elif [[ "$BRANCH_EXISTS_REMOTE" -eq 1 ]]; then
WORKTREE_ADD_ARGS=(-b "$BRANCH" "$WORKTREE_DIR" "origin/$BRANCH")
echo "new-ticket-worktree.sh: [INFO] reusing existing remote branch origin/$BRANCH"
else
if ! git -C "$DAOS_MAIN_REPO" show-ref --verify --quiet "refs/remotes/origin/$BASE"; then
echo "new-ticket-worktree.sh: [ERROR] base ref origin/$BASE not found -- fetch first: git -C \"$DAOS_MAIN_REPO\" fetch origin" >&2
exit 1
fi
WORKTREE_ADD_ARGS=(-b "$BRANCH" "$WORKTREE_DIR" "origin/$BASE")
echo "new-ticket-worktree.sh: [INFO] creating new branch $BRANCH from origin/$BASE"
fi
fi

if [[ "$DRY_RUN" -eq 1 ]]; then
if [[ "$EXISTING_WORKTREE" -eq 1 ]]; then
echo "new-ticket-worktree.sh: [INFO] (dry-run) would run: $DEPLOY_ENV $WORKTREE_DIR"
else
echo "new-ticket-worktree.sh: [INFO] (dry-run) would run: git -C $DAOS_MAIN_REPO worktree add ${WORKTREE_ADD_ARGS[*]}"
echo "new-ticket-worktree.sh: [INFO] (dry-run) would run: git -C $WORKTREE_DIR submodule update --init --recursive"
echo "new-ticket-worktree.sh: [INFO] (dry-run) would run: $DEPLOY_ENV $WORKTREE_DIR"
fi
if [[ -n "$SKELETON_FROM" ]]; then
echo "new-ticket-worktree.sh: [INFO] (dry-run) would seed skeleton from $(normalize_ticket "$SKELETON_FROM")"
fi
echo "$WORKTREE_DIR"
exit 0
fi

if [[ "$EXISTING_WORKTREE" -eq 1 ]]; then
"$DEPLOY_ENV" "$WORKTREE_DIR"
echo "new-ticket-worktree.sh: [INFO] re-synced env files for existing worktree"
else
mkdir -p "$TICKET_DIR"
git -C "$DAOS_MAIN_REPO" worktree add "${WORKTREE_ADD_ARGS[@]}"
git -C "$WORKTREE_DIR" submodule update --init --recursive
"$DEPLOY_ENV" "$WORKTREE_DIR"
fi

if [[ -n "$SKELETON_FROM" ]]; then
SRC_TICKET="$(normalize_ticket "$SKELETON_FROM")"
if [[ ! "$SRC_TICKET" =~ ^DAOS-[0-9]+$ ]]; then
echo "new-ticket-worktree.sh: [ERROR] invalid --skeleton-from ticket ID: $SRC_TICKET" >&2
exit 1
fi
SRC_TICKET_LC="$(echo "$SRC_TICKET" | tr '[:upper:]' '[:lower:]')"
SRC_DIR="$DAOS_TICKETS_DIR/$SRC_TICKET"

if [[ ! -d "$SRC_DIR" ]]; then
echo "new-ticket-worktree.sh: [ERROR] --skeleton-from ticket dir not found: $SRC_DIR" >&2
exit 1
fi

echo "new-ticket-worktree.sh: [INFO] seeding skeleton from $SRC_DIR into $TICKET_DIR"

COPIED_FILES=()

SKELETON_GLOBS=(
"env.sh" "install-daos.sh" "update-daos.sh" "cleanup.sh"
"start-daos.sh" "stop-daos.sh" "inventory.yml" "run-*.sh" "check-*.sh"
)
for pattern in "${SKELETON_GLOBS[@]}"; do
for f in "$SRC_DIR"/$pattern; do
[[ -e "$f" ]] || continue
dest="$TICKET_DIR/$(basename "$f")"
cp "$f" "$dest"
COPIED_FILES+=("$dest")
echo "new-ticket-worktree.sh: [INFO] copied $(basename "$f")"
done
done

for sub in files setup; do
if [[ -d "$SRC_DIR/$sub" ]]; then
cp -r "$SRC_DIR/$sub" "$TICKET_DIR/"
while IFS= read -r -d '' f; do
COPIED_FILES+=("$f")
done < <(find "$TICKET_DIR/$sub" -type f -print0)
echo "new-ticket-worktree.sh: [INFO] copied $sub/"
fi
done

if [[ -d "$SRC_DIR/tests" ]]; then
mkdir -p "$TICKET_DIR/tests"
for f in "$SRC_DIR"/tests/*.c "$SRC_DIR"/tests/Makefile; do
[[ -e "$f" ]] || continue
dest="$TICKET_DIR/tests/$(basename "$f")"
cp "$f" "$dest"
COPIED_FILES+=("$dest")
echo "new-ticket-worktree.sh: [INFO] copied tests/$(basename "$f")"
done
fi

for f in "${COPIED_FILES[@]}"; do
if grep -qF -e "$SRC_TICKET" -e "$SRC_TICKET_LC" "$f" 2>/dev/null; then
sed -i "s#$SRC_TICKET#$TICKET#g; s#$SRC_TICKET_LC#$TICKET_LC#g" "$f"
echo "new-ticket-worktree.sh: [INFO] substituted ticket ID in ${f#"$TICKET_DIR"/}"
fi
done

if [[ -f "$TICKET_DIR/env.sh" ]]; then
sed -i \
-e "s#^DAOS_SRC=.*#DAOS_SRC=\"$WORKTREE_DIR\"#" \
-e "s#^DAOS_BUILD=.*#DAOS_BUILD=/var/tmp/daos-build-$TICKET_LC#" \
"$TICKET_DIR/env.sh"
echo "new-ticket-worktree.sh: [INFO] rewrote env.sh: DAOS_SRC=$WORKTREE_DIR DAOS_BUILD=/var/tmp/daos-build-$TICKET_LC"
fi

if [[ -f "$TICKET_DIR/inventory.yml" ]]; then
sed -i \
-e "s#^\([[:space:]]*daos_source_dir:\).*#\1 $WORKTREE_DIR#" \
-e "s#^\([[:space:]]*daos_build_dir:\).*#\1 /var/tmp/daos-build-$TICKET_LC#" \
"$TICKET_DIR/inventory.yml"
echo "new-ticket-worktree.sh: [INFO] rewrote inventory.yml: daos_source_dir=$WORKTREE_DIR daos_build_dir=/var/tmp/daos-build-$TICKET_LC"
fi

echo "new-ticket-worktree.sh: [INFO] skeleton seeded from $SRC_TICKET -- REVIEW THE DIFF before trusting it (substitution is textual, not semantic)"
echo "new-ticket-worktree.sh: [INFO] to regenerate ansible-templated scripts (daos-make.sh, etc.) from the rewritten inventory.yml, run manually:"
echo "new-ticket-worktree.sh: [INFO]   cd <ansible/ftest checkout> && ansible-playbook -i $TICKET_DIR/inventory.yml ftest.yml --tags dev"
fi

echo "new-ticket-worktree.sh: [INFO] ready -- cd $WORKTREE_DIR && direnv allow"
echo "$WORKTREE_DIR"
