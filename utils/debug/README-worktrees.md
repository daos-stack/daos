### Overview

Parallel per-ticket DAOS source checkouts, so several DAOS-JIRA tickets can
be built/edited/tested at once without fighting over the single
`~/work/daos` checkout. Two pieces:

- `~/work/daos-tools` — a **permanent** `git worktree` of this very branch
  (`ckochhof/dbg/master`), so these scripts (and the env-setup files below)
  are always available at a stable path, regardless of which branch
  `~/work/daos` itself happens to be on.
- `new-ticket-worktree.sh` / `remove-ticket-worktree.sh` — create/remove a
  per-ticket `git worktree` under `~/work/tickets/daos-jira/DAOS-<N>/daos`.

PR review is a **different** mechanism entirely (`git clone --reference` +
`gh pr checkout`, driven by the `pr-review` Copilot skill, `repo/` instead
of `daos/`) — not something these scripts manage.


### Why not just `git clone --reference` for dev/fix too?

A linked worktree shares the *same* `.git` (objects, refs, config) as
`~/work/daos` — a `git fetch` in either place is immediately visible
everywhere, and there's no risk of the clone becoming stale/corrupted if
`~/work/daos` is ever aggressively `gc`'d (a real risk with
`--reference`, since it borrows objects from another repo instead of
being that repo). It's also instant to create/remove (no network, no
~858MB history to re-fetch).


### The env-setup problem this also had to fix

`.envrc` (direnv) + `setup-c.sh`/`setup-python.sh`/`setup-golang.sh`/
`setup-dev.sh` (`C_INCLUDE_PATH`/`CGO_CFLAGS`/`CGO_LDFLAGS`/
`LD_LIBRARY_PATH`/`PYTHONPATH`, needed for nvim's clangd/gopls/pyright to
work) are all **untracked** files (see `.git/info/exclude`), so a fresh
`git worktree` starts with none of them. Worse, each one used to default
`DAOS_SRC_DIR` to the hardcoded `$HOME/work/daos` — so even copied by hand
into a new worktree, they'd silently point back at the wrong tree's build.

Fixed two ways:
1. Each script's `DAOS_SRC_DIR` default is now its own directory
   (`realpath "$(dirname "${BASH_SOURCE[0]}")"`), not a fixed path.
2. `deploy-daos-env.sh <target-dir>` symlinks the 4 `setup-*.sh` back to
   **this** directory (fix a bug once here, every deployment — past and
   future — picks it up immediately) and copies `envrc` -> `.envrc` (a
   plain file, not symlinked, so one specific ticket can override it
   locally without affecting any other worktree). Both
   `new-ticket-worktree.sh` and the `pr-review` skill's
   `generate_pr_review.py` call this same helper, so review clones
   (`repo/`) get the same nvim support as dev/fix worktrees.


### Day-to-day usage

```
# Create/attach a ticket worktree (auto-picks the next patch number):
~/work/daos-tools/utils/debug/scripts/new-ticket-worktree.sh --ticket DAOS-17321 --type dev

# ... then, once built (scons --config=force):
cd ~/work/tickets/daos-jira/DAOS-17321/daos && direnv allow

# See every ticket worktree at a glance:
~/work/daos-tools/utils/debug/scripts/new-ticket-worktree.sh --list

# Done with a ticket:
~/work/daos-tools/utils/debug/scripts/remove-ticket-worktree.sh --ticket DAOS-17321
```

Seed a brand-new ticket's build/deploy/test scripts from an existing one
(rewrites `DAOS_SRC`/`daos_source_dir` to the new worktree and gives
`DAOS_BUILD`/`daos_build_dir` a ticket-unique suffix — see next section):

```
~/work/daos-tools/utils/debug/scripts/new-ticket-worktree.sh --ticket DAOS-XXXXX --skeleton-from DAOS-17321
```

This is a **textual, not semantic** substitution — review the diff before
trusting it. It never runs `ansible-playbook` itself; it only prints the
exact command to regenerate the templated scripts (`daos-make.sh`, etc.)
from the rewritten `inventory.yml`.

Run `--help` on either script for the full flag list, or `--dry-run` to
preview without touching anything.


### The `DAOS_BUILD` collision this also avoids

`check-ddb_build_freshness.sh` (see `DAOS-17321/README.md`) documented a
real bug: scons's `.sconsign.dblite` build-signature database is keyed by
absolute path *strings*. Two sandboxes building from the same
`DAOS_SRC=$HOME/work/daos` with the same `DAOS_BUILD=/var/tmp/daos-build`
on the same build node caused scons to silently skip recompiling a
changed file, because the signature looked identical even though the
underlying content differed. Separate worktrees already fix the
`DAOS_SRC` half (different absolute path per ticket); `--skeleton-from`
also gives each ticket its own `DAOS_BUILD=/var/tmp/daos-build-<ticket>`,
closing the other half.


### Files

| File | Purpose |
|---|---|
| `scripts/envrc` | direnv entry point — `source ./setup-dev.sh`. Copied (not symlinked) to `.envrc` in every deployment target. |
| `scripts/setup-dev.sh` | Sources the three below, in order. Self-locating `DAOS_SRC_DIR` default. |
| `scripts/setup-c.sh` | `C_INCLUDE_PATH`/`CPATH`/`PKG_CONFIG_PATH` from `.build_vars.sh`. |
| `scripts/setup-python.sh` | `PYTHONPATH` from the source tree + installed extensions. |
| `scripts/setup-golang.sh` | `CGO_CFLAGS`/`CGO_LDFLAGS`/`LD_LIBRARY_PATH` from `.build_vars.sh`. |
| `scripts/deploy-daos-env.sh` | Symlinks the 4 `setup-*.sh` + copies `envrc` -> `.envrc` into any target directory. |
| `scripts/new-ticket-worktree.sh` | Create/re-sync a per-ticket `git worktree`, optionally seeded from another ticket's skeleton. |
| `scripts/remove-ticket-worktree.sh` | Symmetric teardown, refuses on uncommitted/unpushed changes unless `--force`. |
