#!/bin/bash
# Propagates a change made in one ticket worktree through a stack of
# dependent ticket worktrees (one branch per sub-task, each checked out in
# its own per-ticket worktree created by new-ticket-worktree.sh).
#
# `git rebase --update-refs` cannot do this: it never updates a branch that
# is checked out in another worktree. So, for each consecutive pair
# (lower, upper) of the given tickets, this script runs in upper's worktree:
#
#   git rebase --onto <lower tip> HEAD~<n>
#
# where <n> is the number of leading commits of upper's branch whose subject
# starts with upper's ticket ID (the DAOS commit convention `DAOS-NNNNN ...`).
# Before rebasing it checks that the commit right below those (HEAD~n) is a
# commit of the lower ticket, and skips the pair when it already is the lower
# tip (so rerunning the command is harmless -- it is idempotent).
#
# On a conflict the script stops in the affected worktree and prints what to
# do (`git rebase --continue` there, then rerun the very same command: the
# pairs already rebased are detected as up to date and skipped).
#
# With --umbrella, the integration ticket's branch (a worktree that carries
# no commit of its own and just points at the top of the stack) is reset to
# the new top, but only if it pointed at the old top or at some other commit
# of the top ticket (a stale mirror, e.g. after a conflict-interrupted run);
# a branch pointing anywhere else is left alone.
#
# Nothing is ever pushed. Every worktree involved must be clean (no
# uncommitted tracked changes, no rebase in progress).
#
# Usage:
#   rebase-stack.sh [--dry-run] [--umbrella DAOS-NNNNN] DAOS-A DAOS-B [DAOS-C ...]
#   rebase-stack.sh --help
#
# Example (DAOS-17817 Phase 1, after amending the DAOS-19577 commit):
#   rebase-stack.sh --umbrella DAOS-17817 DAOS-19577 DAOS-19578 DAOS-19579 DAOS-19581
#
# Env overrides:
#   DAOS_TICKETS_DIR Base directory for per-ticket workspaces (default: ~/work/tickets/daos-jira)

set -euo pipefail

DAOS_TICKETS_DIR="${DAOS_TICKETS_DIR:-$HOME/work/tickets/daos-jira}"
# Upper bound on the number of leading commits attributed to one ticket --
# hitting it means the subjects do not follow the `DAOS-NNNNN ...` convention.
MAX_TICKET_COMMITS=50

DRY_RUN=0
UMBRELLA=""
TICKETS=()

usage() {
cat <<'EOF'
Usage:
  rebase-stack.sh [--dry-run] [--umbrella DAOS-NNNNN] DAOS-A DAOS-B [DAOS-C ...]
  rebase-stack.sh --help

Rebases each ticket's own commits (leading commits whose subject starts with
the ticket ID) onto the tip of the ticket listed before it, bottom to top, in
each ticket's own worktree (DAOS_TICKETS_DIR/<ticket>/daos). Pairs already in
place are skipped, so rerunning after a conflict resolution is safe.

Flags:
  --umbrella DAOS-NNNNN  Integration ticket whose branch only mirrors the top of
                         the stack: reset it to the new top if it was at the old
                         one (or at another commit of the top ticket)
  --dry-run              Print what would be rebased without touching anything
  --help                 Show this help

Env overrides:
  DAOS_TICKETS_DIR Base directory for per-ticket workspaces (default: ~/work/tickets/daos-jira)
EOF
}

info() { echo "rebase-stack.sh: [INFO] $*"; }
warn() { echo "rebase-stack.sh: [WARN] $*" >&2; }
die()  { echo "rebase-stack.sh: [ERROR] $*" >&2; exit 1; }

normalize_ticket() {
local t
t="$(echo "$1" | tr '[:lower:]' '[:upper:]')"
if [[ "$t" =~ ^[0-9]+$ ]]; then
t="DAOS-$t"
fi
[[ "$t" =~ ^DAOS-[0-9]+$ ]] || die "invalid ticket ID: $1"
echo "$t"
}

while [[ $# -gt 0 ]]; do
case "$1" in
--umbrella) UMBRELLA="$(normalize_ticket "$2")"; shift 2 ;;
--dry-run) DRY_RUN=1; shift ;;
--help|-h) usage; exit 0 ;;
-*) echo "rebase-stack.sh: [ERROR] unknown argument: $1" >&2; usage >&2; exit 1 ;;
*) TICKETS+=("$(normalize_ticket "$1")"); shift ;;
esac
done

[[ ${#TICKETS[@]} -ge 2 ]] || { usage >&2; die "at least two tickets are required (lower first, top of the stack last)"; }

worktree_of() {
local dir="$DAOS_TICKETS_DIR/$1/daos"
[[ -d "$dir" ]] || die "no worktree directory for $1: $dir"
git -C "$dir" rev-parse --is-inside-work-tree >/dev/null 2>&1 || die "$dir is not a git worktree"
echo "$dir"
}

branch_of() {
git -C "$1" symbolic-ref -q --short HEAD || die "$1 is in detached HEAD state -- check out the ticket branch first"
}

require_clean() {
local wt="$1" ticket="$2"
if [[ -d "$(git -C "$wt" rev-parse --git-path rebase-merge)" || -d "$(git -C "$wt" rev-parse --git-path rebase-apply)" ]]; then
die "a rebase is already in progress in $wt ($ticket) -- finish it (git rebase --continue / --abort) and rerun"
fi
if [[ -n "$(git -C "$wt" status --porcelain --untracked-files=no --ignore-submodules=dirty)" ]]; then
die "$wt ($ticket) has uncommitted changes -- commit or stash them first"
fi
}

subject_matches() { [[ "$2" =~ ^$1([[:space:]:]|$) ]]; }

# Number of leading commits of HEAD whose subject starts with the ticket ID.
own_commit_count() {
local wt="$1" ticket="$2" n=0 subject
while IFS= read -r subject; do
subject_matches "$ticket" "$subject" || break
n=$((n + 1))
done < <(git -C "$wt" log --format=%s -n $((MAX_TICKET_COMMITS + 1)) HEAD)
[[ $n -le $MAX_TICKET_COMMITS ]] || die "more than $MAX_TICKET_COMMITS leading commits of $ticket in $wt -- subjects do not look like a ticket stack"
echo "$n"
}

short() { git -C "$1" rev-parse --short "$2"; }

# Record the stack before touching anything, so the umbrella check and the
# summary refer to the original tips.
declare -A WT OLD_TIP
for ticket in "${TICKETS[@]}"; do
WT[$ticket]="$(worktree_of "$ticket")"
require_clean "${WT[$ticket]}" "$ticket"
OLD_TIP[$ticket]="$(git -C "${WT[$ticket]}" rev-parse HEAD)"
info "$ticket: ${WT[$ticket]} on $(branch_of "${WT[$ticket]}") @ $(short "${WT[$ticket]}" HEAD)"
done

TOP="${TICKETS[-1]}"
OLD_TOP="${OLD_TIP[$TOP]}"
UMBRELLA_WT=""
if [[ -n "$UMBRELLA" ]]; then
UMBRELLA_WT="$(worktree_of "$UMBRELLA")"
require_clean "$UMBRELLA_WT" "$UMBRELLA"
info "$UMBRELLA (umbrella): $UMBRELLA_WT on $(branch_of "$UMBRELLA_WT") @ $(short "$UMBRELLA_WT" HEAD)"
fi

REBASED=0
WOULD_REBASE=0
for ((i = 1; i < ${#TICKETS[@]}; i++)); do
lower="${TICKETS[$((i - 1))]}"
upper="${TICKETS[$i]}"
lower_wt="${WT[$lower]}"
upper_wt="${WT[$upper]}"
lower_tip="$(git -C "$lower_wt" rev-parse HEAD)"

n="$(own_commit_count "$upper_wt" "$upper")"
[[ $n -ge 1 ]] || die "$upper: no leading commit with a subject starting with '$upper' in $upper_wt (HEAD: $(git -C "$upper_wt" log -1 --format=%s))"
base="$(git -C "$upper_wt" rev-parse "HEAD~$n")"
base_subject="$(git -C "$upper_wt" log -1 --format=%s "$base")"
subject_matches "$lower" "$base_subject" || die "$upper: the commit below its own $n commit(s) is not a $lower commit: $(short "$upper_wt" "$base") '$base_subject' -- stack order wrong?"

if [[ "$base" == "$lower_tip" ]]; then
info "$upper: up to date ($n commit(s) already on top of $lower @ $(short "$lower_wt" HEAD))"
continue
fi

if [[ $DRY_RUN -eq 1 ]]; then
info "(dry-run) $upper: would rebase $n commit(s) from $(short "$upper_wt" "$base") onto $lower @ $(short "$lower_wt" HEAD) in $upper_wt"
WOULD_REBASE=$((WOULD_REBASE + 1))
continue
fi

info "$upper: rebasing $n commit(s) onto $lower @ $(short "$lower_wt" HEAD) in $upper_wt"
if ! git -C "$upper_wt" rebase --onto "$lower_tip" "$base"; then
cat >&2 <<EOF
rebase-stack.sh: [ERROR] rebase of $upper stopped in $upper_wt (conflict?).
  Resolve it there, then:  git -C "$upper_wt" rebase --continue
  (or abort with:          git -C "$upper_wt" rebase --abort)
  and rerun this same rebase-stack.sh command -- pairs already rebased are skipped.
EOF
exit 1
fi
REBASED=$((REBASED + 1))
done

NEW_TOP="$(git -C "${WT[$TOP]}" rev-parse HEAD)"

if [[ -n "$UMBRELLA" ]]; then
umbrella_tip="$(git -C "$UMBRELLA_WT" rev-parse HEAD)"
umbrella_subject="$(git -C "$UMBRELLA_WT" log -1 --format=%s HEAD)"
if [[ "$umbrella_tip" != "$OLD_TOP" ]] && ! subject_matches "$TOP" "$umbrella_subject"; then
warn "$UMBRELLA: at $(short "$UMBRELLA_WT" HEAD) '$umbrella_subject', which is neither the old top of the stack nor a $TOP commit -- left untouched"
elif [[ $DRY_RUN -eq 1 && $WOULD_REBASE -gt 0 ]]; then
info "(dry-run) $UMBRELLA: would be reset to the new top of the stack after the rebase"
elif [[ "$umbrella_tip" == "$NEW_TOP" ]]; then
info "$UMBRELLA: already at the top of the stack ($(short "$UMBRELLA_WT" HEAD))"
elif [[ $DRY_RUN -eq 1 ]]; then
info "(dry-run) $UMBRELLA: would be reset to the top of the stack $(short "$UMBRELLA_WT" "$NEW_TOP")"
else
git -C "$UMBRELLA_WT" reset -q --hard "$NEW_TOP"
info "$UMBRELLA: reset to the top of the stack $(short "$UMBRELLA_WT" HEAD)"
fi
fi

echo
echo "Stack ($([[ $DRY_RUN -eq 1 ]] && echo "dry-run, nothing changed" || echo "$REBASED branch(es) rebased")):"
for ticket in "${TICKETS[@]}"; do
new_tip="$(git -C "${WT[$ticket]}" rev-parse HEAD)"
if [[ "$new_tip" == "${OLD_TIP[$ticket]}" ]]; then
printf '  %-12s %s\n' "$ticket" "$(short "${WT[$ticket]}" HEAD)"
else
printf '  %-12s %s -> %s\n' "$ticket" "$(short "${WT[$ticket]}" "${OLD_TIP[$ticket]}")" "$(short "${WT[$ticket]}" HEAD)"
fi
done
if [[ $REBASED -gt 0 ]]; then
echo
info "rebuild the rebased worktrees (build-isolated.sh, or build-daos.sh [--activate] for the one to ftest) and update the Commit lines of their PR drafts"
fi
