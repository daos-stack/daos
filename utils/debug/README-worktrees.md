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


### The branch tree

Personal branches in this repo (`daos-stack/daos`, this user's fork/remote)
follow a fixed naming convention:

```
ckochhof/{dev,fix}/{base}/{ticket-lc}/{patch-NNN}
```

e.g. `ckochhof/fix/master/daos-19299/patch-001` — `dev` vs `fix` is the
work-type segment, `{base}` is the branch forked from (`master`,
`release/2.8`, ...), `{ticket-lc}` is the lowercased ticket ID,
`patch-NNN` an auto-incrementing counter (see `new-ticket-worktree.sh
--patch`). `new-ticket-worktree.sh` creates/reuses exactly these branches
for the per-ticket `daos/` worktrees above.

Two branches sit outside that per-ticket convention:

- **`ckochhof/dbg/master`** — the permanent personal tools branch, checked
  out at `~/work/daos-tools` (this whole `utils/debug/` tree, plus
  `utils/ansible/ftest/`, lives here). Not tied to any single ticket;
  accumulates reusable tooling over time.
- **`ansible/ftest`** — a `git symbolic-ref`, *not* a real branch: it
  points at whatever ticket branch currently holds the actively-developed
  ansible ftest tooling (at the time of writing,
  `ckochhof/dev/master/daos-17397`). Ticket branches carry real, often
  substantial, unrelated DAOS source changes for that ticket alongside
  any tooling work, so `utils/ansible/ftest/` is pulled from there into
  `ckochhof/dbg/master` with a **surgical subtree copy**
  (`git checkout ansible/ftest -- utils/ansible/ftest`), never a full
  branch merge — merging would also pull in everything else on that
  ticket branch. Check `git symbolic-ref refs/heads/ansible/ftest` if
  this pointer ever needs to move to a newer ticket branch.


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


### Building and testing a ticket in isolation

`new-ticket-worktree.sh` also renders a ticket-specific `env.sh`,
`inventory.yml` and `README.md` (via `generate-daos-env.sh`, see the
generated `README.md` for the full walkthrough) so a ticket's build never
collides with another's:

```
# 1. generate env.sh/inventory.yml/README.md (done automatically for new tickets)
~/work/daos-tools/utils/debug/scripts/generate-daos-env.sh --ticket DAOS-17321 --worktree ~/work/tickets/daos-jira/DAOS-17321/daos

# 2. provision the cluster for this ticket (manual, deliberate -- review
#    inventory.yml first; see the ticket's README.md for what this changes
#    on the shared cluster)
cd ~/work/tickets/daos-jira/DAOS-17321 && ./provision-daos.sh

# 3. build/install into this ticket's own isolated prefix, reusing shared prereqs
cd ~/work/tickets/daos-jira/DAOS-17321 && ./build-daos.sh --force --deps

# 4. run standalone unit tests (isolated, safe regardless of what's "live")
./run-vos_tests.sh

# 5. functional tests (exclusive: ftest drives the cluster-wide systemd units,
#    so make this ticket the live one first); defaults from FTEST_* in env.sh,
#    ./files/ftest/ overlaid onto the install tree before each run
./build-daos.sh --activate && ./run-ftest.sh PoolCreateSlowSvc
```

Steps 1-4 are all isolated per ticket: `provision-daos.sh` (step 2) doesn't
repoint the shared `/etc/ld.so.conf.d`/PAM PATH by default
(`daos_client_manage_system_paths=false`), so it no longer races with
another ticket's provisioning run the way a bare `ansible-playbook` call
would. Only running a *live* `start-daos.sh` multi-node cluster or the
functional tests (both need `build-daos.sh --activate` first) remains
exclusive across tickets —
see the generated `README.md`'s "Isolation model" section for exactly why
(shared systemd units, the activated `ld.so.conf.d` entry, and ultimately
the physical PMEM/NVMe/network hardware on `brd-216..219`).


### The `DAOS_BUILD`/`DAOS_INSTALL` collision this also avoids

`check-ddb_build_freshness.sh` (see `DAOS-17321/README.md`) documented a
real bug: scons's `.sconsign.dblite` build-signature database is keyed by
absolute path *strings*. Two sandboxes building from the same
`DAOS_SRC=$HOME/work/daos` with the same `DAOS_BUILD=/var/tmp/daos-build`
on the same build node caused scons to silently skip recompiling a
changed file, because the signature looked identical even though the
underlying content differed. Separate worktrees already fix the
`DAOS_SRC` half (different absolute path per ticket); `generate-daos-env.sh`
(used by default for every new ticket, see previous section) gives each
ticket its own `DAOS_BUILD=/var/tmp/daos-build-<ticket>` **and**
`DAOS_INSTALL`/`daos_runtime_dir=/scratch/$USER/daos-install-<ticket>`,
closing both halves — previously only `--skeleton-from` gave a ticket its
own `DAOS_BUILD`, and `DAOS_INSTALL` was always the one shared location.


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
| `scripts/generate-daos-env.sh` | Renders a ticket-specific `env.sh`/`inventory.yml`/`README.md` with isolated `DAOS_BUILD`/`DAOS_INSTALL` paths. Skips files that already exist unless `--force`. |
| `scripts/compute-daos-alt-prefix.sh` | Computes the colon-separated scons `ALT_PREFIX` list from a shared install's `.build_vars.sh`, used by `generate-daos-env.sh`. |
| `scripts/build-daos.sh` | Deployed into each ticket dir; ssh + invokes that ticket's ansible-generated `daos-make.sh`, `--build-only` by default (see `--activate`). |
| `scripts/provision-daos.sh` | Deployed into each ticket dir; invoke with no args (or only additional ansible-playbook flags, e.g. `-e`/`--check`/`-vvv`/`--limit` -- never `-i`/a playbook path, both are already hardcoded and rejected if passed). Internally runs `ansible-playbook` against this ticket's `inventory.yml` and `ftest.yml`, passing `-e daos_client_manage_system_paths=false` so provisioning this ticket doesn't repoint the shared ld.so.conf.d/PAM PATH (see `build-daos.sh --activate`). |
| `scripts/run-vos_tests.sh`, `run-ddb_ut.sh`, `run-ddb_tests.sh`, `run-dtx_ut.sh`, `run-dtx_tests.sh`, `run-go_unit.sh` | Deployed into each ticket dir; generic standalone unit-test-suite runners. |
| `scripts/cleanup.sh`, `start-daos.sh`, `stop-daos.sh` | Deployed into each ticket dir; live-cluster lifecycle from the ticket's `files/daos_*-<host>.yml` configs. `start-daos.sh` waits until every engine is joined after the format and creates a pool+container; `start-daos.sh --no-pool` stops after the servers and agents are up (for reproduction scripts that test the pool creation itself). |
| `scripts/run-ftest.sh` | Deployed into each ticket dir; runs launch.py test filters through the ticket's ansible-generated `daos-launch.sh` on `$LOGIN_NODE`, after overlaying the ticket's `files/ftest/` onto its install tree. Defaults (test servers/clients, `--nvme`, `--scm_size`, provider, default filters) come from the `FTEST_*` variables of the generated `env.sh`. |
| `ansible/ftest/` | The DAOS functional-test-platform Ansible playbook/roles (imported from the `ansible/ftest` branch — see "The branch tree" above), extended with `daos_alt_prefix`/`ALT_PREFIX` reuse and `daos-make.sh --build-only` for per-ticket isolated builds. |
