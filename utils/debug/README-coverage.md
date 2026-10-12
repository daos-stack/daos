### Overview

Targeted gcov coverage of a handful of changed C files of a DAOS tree, with
the unit-test suites that exercise them, **without** rebuilding the tree with
`--coverage` and without touching the scons build directory or the install
prefix for longer than one test run. Five helpers, deployed into every ticket
directory by `new-ticket-worktree.sh` (next to the `run-*.sh` runners):

| Helper | Role |
|---|---|
| `cov-gen_cmds.py` | From a `build-isolated.sh` log, re-emit the compile lines of the changed objects with `--coverage` into a side directory, and the link line of their consumer (`libbio.so`, `libddb.so`, `ddb_ut`, ...) with the instrumented objects substituted, as a shell script. |
| `cov-swap_run.sh` | Swap the instrumented shared library into the install prefix, run one command (a suite), restore the build-dir copy (`cmp`-guarded). |
| `cov-gcov.sh` | `gcov -b -c -f` per instrumented object, split into one `.gcov` per source file, written into the side directory (the source tree stays clean). |
| `cov-report.py` | Per-function summary of a `.gcov` (lines executed, branches taken, never-executed lines, never-taken branches); merges several `.gcov` of the same source. |
| `cov-sites.py` | Per-function list of the error-reporting sites (`D_ERROR`, `DL_ERROR`, `D_WARN`, ... or `--regex`) with their hit counts. |

Why not a full `--coverage` build: it is a complete rebuild of the tree plus a
reinstall, the instrumented libraries then leak into every other test run of
the ticket, and nothing of it is needed to answer the usual review question
("which lines/branches of *these* new functions do the tests reach?").
`gcov` (gcc 11) is installed on the build node; `lcov`/`gcovr` are not.

Validated on DAOS-19577 (`~/work/tickets/daos-jira/DAOS-19577/copilot/coverage/`
keeps the exact scripts and `.gcov` files of those runs; `coverage_report.md`
there is an example of the resulting report).


### How it works

1. **Instrument only the changed objects.** The scons build log of the tree
   (`build-isolated.sh` output) contains the exact compile line of every
   object (`gcc -o <BUILD>/src/bio/b_bio_wal.os -c ... src/bio/bio_wal.c`) and
   the link line of every library/binary. `cov-gen_cmds.py` finds them and
   prints a script that re-runs them with `--coverage`, redirecting the
   outputs into a side directory (`$COV_DIR/<name>`, `COV_DIR` from the
   ticket's `env.sh`): the `.gcno` files land there at compile time, the
   `.gcda` files at test run time (gcc records the object's absolute path).
2. **Relink the consumer into the side directory** with `--coverage` and the
   instrumented objects substituted (same script). A shared library
   (`libbio.so`, `libddb.so`) is picked up by the test binaries through the
   install prefix (RPATH); a test binary that links the sources statically
   (`ddb_ut` links `src/utils/ddb/*.o`) is relinked itself.
3. **Run the suite on the instrumented copy.** Shared library:
   `cov-swap_run.sh LIB SIDE -- <suite command>` copies the side-dir library
   over the installed one, runs the command, and restores the **build-dir**
   copy (the reference: the installed copy must already match it, otherwise a
   previous swap was not restored or the install is stale — the script
   refuses). Relinked test binary: run it from the side directory (see the
   RUNPATH note below). Counters accumulate across runs (PMEM `ddb_tests`,
   then `ddb_md_on_ssd`, then ...), so run every relevant suite before step 4.
4. **Annotate and summarize.** From the DAOS source root (the objects were
   compiled with source paths relative to it):
   `cov-gcov.sh SIDE b_bio_wal b_bio_context` → `SIDE/b_bio_wal--src_bio_bio_wal.c.gcov`, ...
   then `cov-report.py FILE.gcov FUNC... [--md]` and
   `cov-sites.py FILE.gcov FUNC... [--md]`.


### Walk-through: a `libbio.so` change tested by `bio_ut` (part 1 of DAOS-19577)

All commands on the build node, from the ticket's `daos/` source root, after
`source ../env.sh` (`DAOS_BUILD`, `DAOS_INSTALL`, `COV_DIR`); the build log is
the one `build-isolated.sh` wrote for this tree (an incremental log lacks the
compile lines of untouched objects — add an older full log as a second
argument). Objects and libraries live in scons' variant directory
`$DAOS_BUILD/daos/<build type>/<compiler>/src/...` (`daos/debug/gcc` for the
default `build-isolated.sh` settings), abbreviated `$V` below.

```bash
SIDE=$COV_DIR/patch-001
V=$DAOS_BUILD/daos/debug/gcc/src
B=$V/bio
../cov-gen_cmds.py --cov-dir $SIDE --target libbio.so \
    --obj $B/b_bio_wal.os --obj $B/b_bio_context.os \
    /tmp/daos19577_build_p1.log > /tmp/cov_p1.sh
sh /tmp/cov_p1.sh                 # compiles the 2 objects + relinks libbio.so into $SIDE
ls $SIDE                          # b_bio_wal.os b_bio_wal.gcno b_bio_context.os ... libbio.so

# bio_ut is an AIO suite of run_utest.py: sudo, 4 GB AIO file, /mnt/daos tmpfs
# -> use run-utest.sh's recipe (or run-utest.sh itself from the ticket dir,
#    with the swap done around it from the build node).
../cov-swap_run.sh libbio.so $SIDE -- \
    sudo -n unshare -m --propagation private -- /bin/sh -c \
    "mount -t tmpfs -o size=32G tmpfs /mnt/daos && mount -t tmpfs -o size=34G tmpfs /tmp && \
     exec sudo -n -u $USER -E env PATH=$PATH ./utils/run_utest.py \
         --suite_filter VOS_md_on_ssd --test_filter bio_ut --log_dir /var/tmp/daos_utest_cov"
ls -la $SIDE/*.gcda               # root-owned (sudo), mode 0644: readable

../cov-gcov.sh $SIDE b_bio_wal b_bio_context
../cov-report.py $SIDE/b_bio_wal--src_bio_bio_wal.c.gcov \
    meta_hdr_verify wal_hdr_verify wal_hdr_tx_verify wal_hdr_check --md
../cov-sites.py  $SIDE/b_bio_wal--src_bio_bio_wal.c.gcov wal_hdr_verify --md
```

Reading the output: `Lines executed` is the count of executable lines hit at
least once; `#####` lines are listed with their source text; "branches never
taken" on `D_ERROR`/`D_WARN` lines are usually the log-mask branches inside
the logging macros, not a test gap — `cov-sites.py` tells which error sites
were actually reached and how many times.


### Variants

- **`libddb.so` with `ddb_tests` (PMEM and MD-on-SSD runs).** Objects are
  `$V/utils/ddb/ddb_<file>.os`; `--target libddb.so`. Run the
  PMEM suite (`$DAOS_INSTALL/bin/ddb_tests`, hard-coded `/mnt/daos`: inside
  the same private-mount `unshare` recipe as above, without `run_utest.py`)
  and the `ddb_md_on_ssd` suite (`./utils/run_utest.py --suite_filter ddb_md_on_ssd`)
  inside one `cov-swap_run.sh` session, e.g. `-- sh -c 'cmd1; cmd2'`, so the
  counters of both accumulate before the restore. (`run-ddb_tests.sh` and
  `run-utest.sh` are the ssh wrappers of those two commands; the swap itself
  must run on the build node, where the build directory is.)
- **`ddb_ut` (static link of the ddb sources).** Objects are
  `$V/utils/ddb/<file>.o` (no `ddb_` prefix, no `s`),
  `--target ddb_ut`; the relinked binary lands in `$SIDE`. Its RUNPATH is
  `$ORIGIN`-relative (`$ORIGIN/../lib64`, `$ORIGIN/../lib64/daos_srv`, and a
  `$ORIGIN/../../../../..`-prefixed prereq path that resolves from `/`), so
  run it from a `bin/` subdirectory with a `lib64` symlink next to it:
  `mkdir -p $SIDE/bin && mv $SIDE/ddb_ut $SIDE/bin/ && ln -s $DAOS_INSTALL/lib64 $SIDE/lib64 && $SIDE/bin/ddb_ut`.
  The `.gcda` of the static object is named after it (`ddb_wal.gcda`) and
  the shared-library one after its object (`ddb_ddb_wal.gcda`): the two
  `.gcov` of the same source can be merged by `cov-report.py` as
  `FILE1.gcov,FILE2.gcov`.
- **Go code** needs none of this: from `src/control`, after
  `source run_go_tests.sh`-style `setup_environment` (see `run-go_unit.sh`),
  `go test -mod vendor -race -tags fault_injection,test_stubs,spdk -count=1 -coverprofile=cover.out ./cmd/ddb/...`
  then `go tool cover -func=cover.out`.


### Gotchas

- **Never leave an instrumented library in the prefix.** `cov-swap_run.sh`
  restores and `cmp`s whatever the command's exit status is, but a killed
  session can leave the swap in place: the next `build-isolated.sh` compares
  nothing and silently keeps it. Check with `cmp $V/bio/libbio.so $DAOS_INSTALL/lib64/daos_srv/libbio.so`.
- **The build-dir copy is the reference**, not a backup of the installed
  file: `scons install` copies the build-dir library verbatim, so a mismatch
  means a stale install or a forgotten swap (the script refuses rather than
  guessing).
- **Root-owned `.gcda`.** Suites run under `sudo` (`run_utest.py`) write the
  counters as root; the side directory under `/var/tmp` must exist and be
  writable by root (it is), the files are mode 0644.
- **`ddb manpage` build step and `libddb.so`.** The scons build runs the
  freshly built `ddb` binary, which loads `libddb.so` from the **installed**
  prefix via RPATH. After a branch switch that changes a struct shared with
  the Go side, that step panics (`go-flags` nil Option) until the build-dir
  library is copied over the installed one (`\cp -f`) — and it also means a
  leftover instrumented `libddb.so` is what that step would load.
- **Incremental build logs** only contain the lines of what was rebuilt;
  keep the full log of the first `build-isolated.sh` of the tree (or pass
  several logs to `cov-gen_cmds.py`, searched in order).
- **`run_utest.py`'s AIO suites** create a 4 GB AIO file per suite entry
  under `/tmp` and leak the descriptor until the driver exits: run them with
  `run-utest.sh --private-mount` (private tmpfs on `/tmp`, see
  `README-worktrees.md`) or directly with the `unshare` recipe above, and
  keep the log directory outside `/tmp`.
