#!/bin/bash
# Removes a per-ticket DAOS git-worktree checkout created by
# new-ticket-worktree.sh. Symmetric teardown -- see README-worktrees.md.
#
# Usage:
#   remove-ticket-worktree.sh --ticket DAOS-NNNNN [--force] [--purge] [--delete-branch] [--dry-run]
#   remove-ticket-worktree.sh --help
#
# Refuses (non-zero exit) if the worktree has uncommitted changes, or the
# branch has commits not present on its upstream (or no upstream configured
# at all), unless --force is passed. By default only unregisters the
# worktree (git worktree remove) -- the ticket directory and the branch are
# left alone; pass --purge to also delete the ticket directory,
# --delete-branch to also delete the local branch.
#
# Note: `git worktree remove` refuses on its own, unconditionally, whenever
# the worktree contains an initialized submodule (all of ours do, via
# raft) -- this is unrelated to this script's OWN --force (which only
# gates on uncommitted/unpushed changes above). Once this script's own
# safety checks pass, the underlying `git worktree remove` is always
# invoked with --force too, purely to get past that submodule check.
#
# Env overrides:
#   DAOS_MAIN_REPO   Main DAOS checkout (default: ~/work/daos)
#   DAOS_TICKETS_DIR Base directory for per-ticket workspaces (default: ~/work/tickets/daos-jira)

set -euo pipefail

DAOS_MAIN_REPO="${DAOS_MAIN_REPO:-$HOME/work/daos}"
DAOS_TICKETS_DIR="${DAOS_TICKETS_DIR:-$HOME/work/tickets/daos-jira}"

TICKET=""
FORCE=0
PURGE=0
DELETE_BRANCH=0
DRY_RUN=0

usage() {
cat <<'EOF'
Usage:
  remove-ticket-worktree.sh --ticket DAOS-NNNNN [--force] [--purge] [--delete-branch] [--dry-run]
  remove-ticket-worktree.sh --help

Flags:
  --ticket DAOS-NNNNN  Ticket ID (required)
  --force              Override refusal on uncommitted changes or commits
                       not present on the branch's upstream (or no upstream)
  --purge              Also delete the ticket directory (DAOS_TICKETS_DIR/<ticket>)
  --delete-branch      Also delete the local branch after unregistering the worktree
  --dry-run            Print planned actions without executing them
  --help               Show this help

Env overrides:
  DAOS_MAIN_REPO   Main DAOS checkout (default: ~/work/daos)
  DAOS_TICKETS_DIR Base directory for per-ticket workspaces (default: ~/work/tickets/daos-jira)
EOF
}

while [[ $# -gt 0 ]]; do
case "$1" in
--ticket) TICKET="$2"; shift 2 ;;
--force) FORCE=1; shift ;;
--purge) PURGE=1; shift ;;
--delete-branch) DELETE_BRANCH=1; shift ;;
--dry-run) DRY_RUN=1; shift ;;
--help|-h) usage; exit 0 ;;
*) echo "remove-ticket-worktree.sh: [ERROR] unknown argument: $1" >&2; usage >&2; exit 1 ;;
esac
done

if [[ -z "$TICKET" ]]; then
echo "remove-ticket-worktree.sh: [ERROR] --ticket is required" >&2
exit 1
fi

TICKET="$(echo "$TICKET" | tr '[:lower:]' '[:upper:]')"
if [[ "$TICKET" =~ ^[0-9]+$ ]]; then
TICKET="DAOS-$TICKET"
fi
if [[ ! "$TICKET" =~ ^DAOS-[0-9]+$ ]]; then
echo "remove-ticket-worktree.sh: [ERROR] invalid ticket ID: $TICKET (expected DAOS-NNNNN)" >&2
exit 1
fi

if [[ ! -d "$DAOS_MAIN_REPO" ]] || ! git -C "$DAOS_MAIN_REPO" rev-parse --git-dir >/dev/null 2>&1; then
echo "remove-ticket-worktree.sh: [ERROR] DAOS_MAIN_REPO is not a git repository: $DAOS_MAIN_REPO" >&2
exit 1
fi

TICKET_DIR="$DAOS_TICKETS_DIR/$TICKET"
WORKTREE_DIR="$TICKET_DIR/daos"

if [[ ! -d "$WORKTREE_DIR" ]]; then
echo "remove-ticket-worktree.sh: [ERROR] no worktree found at $WORKTREE_DIR" >&2
exit 1
fi

REAL_WORKTREE_DIR="$(realpath "$WORKTREE_DIR")"
if ! git -C "$DAOS_MAIN_REPO" worktree list --porcelain | grep -qxF "worktree $REAL_WORKTREE_DIR"; then
echo "remove-ticket-worktree.sh: [ERROR] $WORKTREE_DIR is not a registered git worktree of $DAOS_MAIN_REPO -- refusing to touch it" >&2
exit 1
fi

BRANCH="$(git -C "$WORKTREE_DIR" rev-parse --abbrev-ref HEAD)"

DIRTY=0
if [[ -n "$(git -C "$WORKTREE_DIR" status --porcelain 2>/dev/null)" ]]; then
DIRTY=1
fi

UNPUSHED=0
UPSTREAM=""
if UPSTREAM="$(git -C "$WORKTREE_DIR" rev-parse --abbrev-ref '@{upstream}' 2>/dev/null)"; then
AHEAD="$(git -C "$WORKTREE_DIR" rev-list "$UPSTREAM..HEAD" --count)"
[[ "$AHEAD" -gt 0 ]] && UNPUSHED=1
else
UNPUSHED=1
UPSTREAM="(none configured)"
fi

echo "remove-ticket-worktree.sh: [INFO] ticket=$TICKET worktree=$WORKTREE_DIR branch=$BRANCH upstream=$UPSTREAM dirty=$DIRTY unpushed=$UNPUSHED"

if [[ "$DIRTY" -eq 1 && "$FORCE" -ne 1 ]]; then
echo "remove-ticket-worktree.sh: [ERROR] worktree has uncommitted changes -- commit/stash first, or pass --force" >&2
exit 1
fi
if [[ "$UNPUSHED" -eq 1 && "$FORCE" -ne 1 ]]; then
echo "remove-ticket-worktree.sh: [ERROR] branch has commits not present on its upstream ($UPSTREAM) -- push first, or pass --force" >&2
exit 1
fi

if [[ "$DRY_RUN" -eq 1 ]]; then
echo "remove-ticket-worktree.sh: [INFO] (dry-run) would run: git -C $DAOS_MAIN_REPO worktree remove --force $WORKTREE_DIR"
[[ "$DELETE_BRANCH" -eq 1 ]] && echo "remove-ticket-worktree.sh: [INFO] (dry-run) would run: git -C $DAOS_MAIN_REPO branch -D $BRANCH"
[[ "$PURGE" -eq 1 ]] && echo "remove-ticket-worktree.sh: [INFO] (dry-run) would run: rm -rf $TICKET_DIR"
exit 0
fi

git -C "$DAOS_MAIN_REPO" worktree remove --force "$WORKTREE_DIR"
echo "remove-ticket-worktree.sh: [INFO] worktree unregistered: $WORKTREE_DIR"

if [[ "$DELETE_BRANCH" -eq 1 ]]; then
git -C "$DAOS_MAIN_REPO" branch -D "$BRANCH"
echo "remove-ticket-worktree.sh: [INFO] branch deleted: $BRANCH"
fi

if [[ "$PURGE" -eq 1 ]]; then
rm -rf "$TICKET_DIR"
echo "remove-ticket-worktree.sh: [INFO] ticket directory purged: $TICKET_DIR"
fi
