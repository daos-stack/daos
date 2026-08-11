# Node Local Test (NLT)

`utils/node_local_test.py` is a single-node integration and smoke-test harness for DAOS. It boots
a local DAOS stack (server engine, agent, optional dfuse mount), runs a suite of functional and
fault-injection tests against it, analyzes all daemon and client logs for anomalies, and emits
structured CI artifacts.

## Overview

NLT runs as part of the CI pipeline on a dedicated VM. The entry point is
`ci/unit/test_nlt.sh`, which rsyncs the build to the node and runs
`ci/unit/test_nlt_node.sh` via SSH. The node script installs DAOS, creates a Python venv,
mounts `nlt_logs/` on tmpfs, and execs `node_local_test.py`. After the run,
`ci/unit/test_nlt_post.sh` rsyncs logs and result artifacts back to Jenkins.

## Run modes

`node_local_test.py` accepts one or more positional `mode` arguments:

| Mode | What runs |
|------|-----------|
| `all` (default) | Full POSIX test suite + dfuse multi-mount + UNS overlay + pydaos KV tests + 3 dfuse FI tests |
| `fi` | Exhaustive allocation-failure sweep across many DAOS client commands (no POSIX suite) |
| `launch` | Start the server only and drop to a shell (for interactive debugging) |
| `set-fi` | Start the server, enable fault injection, and exit (used by other tooling) |

The CI pipeline runs two separate stages using these modes:

- **NLT stage**: `mode=all` with valgrind memcheck enabled. Runs the full functional suite plus
  a small set of dfuse FI tests. Typical duration ~20 minutes.
- **Fault injection testing stage**: `mode=fi` with memcheck disabled, server logging at `WARN`,
  and a 14-CPU VM. Runs the full allocation-failure sweep in parallel Docker containers.
  Typical duration up to 4 hours.

## Test suites

### POSIX test suite (`mode=all`)

`PosixTests` contains ~55 test methods that exercise dfuse and the DAOS POSIX layer. Each test
gets its own freshly created POSIX container for isolation. Tests run in parallel (up to 4
threads) with slow tests (`test_uns_basic`, `test_daos_fs_tool`, `test_stable_cont_inode`)
sorted to the front to minimize wall time.

Tests are decorated to control how they run:

- `@needs_dfuse`: runs twice — once with caching disabled, once with caching enabled.
- `@needs_dfuse_with_opt`: similar but with configurable option variants.
- Plain methods: run once against the pool/container directly (no dfuse mount).

This produces approximately 82 total test invocations from the ~55 base methods.

After the POSIX suite, `mode=all` also runs:

- `run_dfuse`: multi-mount dfuse stress tests.
- `run_duns_overlay_test`: UNS overlay tests.
- `test_pydaos_kv` / `test_pydaos_kv_obj_class`: Python pydaos KV API tests.
- `server.set_fi()`: a brief server-side fault injection pass.

### Fault injection (FI) tests

FI tests use the `AllocFailTest` class to verify that every `D_ALLOC()` call site in a given
code path handles allocation failure correctly. For each test:

1. The command runs once baseline (no injection) to establish expected behaviour.
2. The command is re-run repeatedly, injecting a failure at allocation site `fid=2, 3, 4, ...`
   in parallel batches.
3. The sweep stops when an iteration triggers no injection (`NLTestNoFi`), meaning all
   allocation points have been exhausted.
4. Any run that crashes is automatically re-run under valgrind for leak and signal detail.

**FI tests that run in `mode=fi`** (designed for parallel Docker execution):

| Test | Command under test |
|------|--------------------|
| `test_dfuse_start` | dfuse startup error paths |
| `test_alloc_fail` | `daos cont list` |
| `test_fi_cont_query` | `daos cont query` |
| `test_fi_cont_check` | `daos cont check` |
| `test_fi_get_attr` | `daos cont get-attr` |
| `test_fi_list_attr` | `daos cont list-attr` |
| `test_fi_get_prop` | `daos cont get-prop` |
| `test_alloc_fail_copy` | `daos filesystem copy` (read + write) |
| `test_alloc_fail_copy_trunc` | `daos filesystem copy` with truncation |
| `test_alloc_cont_create` | `daos cont create` (with properties) |

**FI tests that run in `mode=all`** (require a live dfuse mount, cannot run in Docker):

| Test | Command under test |
|------|--------------------|
| `test_alloc_fail_cont_create` | `daos cont create --path` via UNS |
| `test_alloc_fail_cat` | `cat` via interception library (IL) |
| `test_alloc_fail_il_cp` | `cp` via interception library (IL) |

### Restart and valgrind server checks

After the main test pass (`mode=all`), NLT:

1. Starts a second server instance (`test_class='restart'`) and immediately stops it — verifying
   the server can cleanly restart against existing storage.
2. Optionally starts a third server instance under valgrind (`--server-valgrind`) and runs
   basic pool and container queries to check for server-side leaks.

## Log analysis (`cart_logtest.py`)

After each command or daemon exits, `node_local_test.py` calls `log_test()`, which dynamically
imports `src/tests/ftest/cart/util/cart_logtest.py` and runs it against the DAOS debug log for
that process. `cart_logtest` parses the structured DAOS log format and flags anomalies.

The `WarningsFactory` instance from `node_local_test.py` is injected into `cart_logtest` so
all findings are written directly into the appropriate warnings JSON file.

### Severity levels

| Severity | Meaning |
|----------|---------|
| `ERROR` | Process crashed, harness shutdown without clean close, teardown failure |
| `HIGH` | `ERR`-level log line in a strict-mode source file — likely a real DAOS bug |
| `NORMAL` | Anomaly that may indicate a problem: wrong error code, excessive logging, RPC lifecycle issue, or allocation failure logged twice within 5 lines of the same file during FI |
| `LOW` | Convention issue (e.g. error code formatted with `%d` instead of `DF_RC`) |

### What `NORMAL: Logging allocation failure` means

This specific entry is emitted when, during a fault injection run, the injected `D_ALLOC()`
failure causes a `-1009` (DER_NOMEM) log line to appear within 5 source lines of the injection
point in the same file. It indicates that the failure is being propagated and logged correctly —
the code is handling the OOM — but that two adjacent call sites both log the same error. This is
a log verbosity pattern, not a correctness bug.

### Output file mapping

| File | Written by | Content |
|------|-----------|---------|
| `nlt-errors.json` | `wf` (main harness + FI sweep) | General anomalies from all log analysis |
| `nlt-server-leaks.json` | `wf_server` | Server log analysis: leaks, opcode state, strict-mode warnings |
| `nlt-client-leaks.json` | `wf_client` | Client FI result checks (`mode=fi` only) |

## CI quality gates

Jenkins uses the Warnings Next Generation plugin (`recordIssues`) to evaluate `nlt-errors.json`
and `nlt-server-leaks.json` against a reference build from `master`. The gates are:

| Gate | Threshold | Result |
|------|-----------|--------|
| Total `ERROR` severity | ≥ 1 | UNSTABLE |
| Total `HIGH` severity | ≥ 1 | UNSTABLE |
| **New** `NORMAL` severity (vs reference) | ≥ 1 | UNSTABLE |
| **New** `LOW` severity (vs reference) | ≥ 1 | UNSTABLE |
| Server leaks total (any severity) | ≥ 1 | UNSTABLE |

"New" means an issue present in the current build that was not fingerprinted in the reference
master build. Jenkins fingerprints issues by file, line number, and message — so changing the
message text of an existing check will cause it to re-appear as "new" on the next build until
the reference catches up.

Note that `NORMAL: Logging allocation failure` entries are expected in passing builds wherever
FI tests exercise code paths with adjacent error logging. These appear as "outstanding" (not
"new") against a stable reference. A PR that adds new `D_ALLOC()` call sites in such paths
will produce a new entry and trip the `NEW_NORMAL` gate.

## Artifacts

### Primary triage artifact

- `nlt-summary.json`: compact run summary for humans and CI automation.
  - Contains run metadata (`run_id`, mode, class name, repeat, engine count).
  - Contains pass/fail result and top high-severity findings (`HIGH`/`ERROR`).
  - Contains per-file issue counts for warnings JSON artifacts.
  - Contains artifact-presence flags (`nlt-junit.xml`, `nlt_logs`, log-usage exports).

### Compatibility artifacts

- `nlt-junit.xml`: JUnit test-case output consumed by test-result publishers.
- `nlt-errors.json`: warnings stream for core log file errors.
- `nlt-server-leaks.json`: warnings stream focused on server leak checks.
- `nlt-client-leaks.json`: warnings stream focused on client leak checks (`mode=fi` only).

All warnings JSON entries include a `runId` (UUID generated at startup) so findings can be
correlated across all outputs from the same NLT execution.

### Log artifacts

- `nlt_logs/dnt_*.log.bz2`: compressed DAOS debug log for every process invocation. Each file
  is named with a `dnt_` prefix and a short descriptor of the test or daemon that produced it
  (e.g. `dnt_dfuse_test_rename_caching_off_<id>.log.bz2`,
  `dnt_server_Server.first_0_<id>.log.bz2`).
- `dnt*.memcheck.xml`: valgrind memcheck output per test invocation.
- `nltir.xml` / `nltr.json`: optional log-usage reports (generated when `--log-usage-save` /
  `--log-usage-import` are passed).

## Running NLT locally

```bash
# Full functional suite (requires installed DAOS at /opt/daos)
python3 utils/node_local_test.py all

# Run a single named test
python3 utils/node_local_test.py --test rename

# Run without valgrind (faster)
python3 utils/node_local_test.py --memcheck no all

# Run the FI sweep only
python3 utils/node_local_test.py fi

# Start server interactively for debugging
python3 utils/node_local_test.py launch

# List all available tests
python3 utils/node_local_test.py --test list
```

Key options:

| Option | Default | Description |
|--------|---------|-------------|
| `--memcheck` | `some` | Valgrind coverage: `yes` (all), `some` (most), `no` (none) |
| `--server-debug` | `DEBUG` | Server log level |
| `--engine-count` | `1` | Number of DAOS engines to start |
| `--repeat` | `1` | Number of times to repeat the full test pass |
| `--test` | all | Run only specific named test(s) |
| `--exclude-test` | none | Exclude specific test(s) from the suite |
| `--dfuse-dir` | `/tmp` | Parent directory for dfuse mounts |

## Primary triage artifact

- `nlt-summary.json`: compact run summary for humans and CI automation.
  - Contains run metadata (`run_id`, mode, class name, repeat, engine count).
  - Contains pass/fail result and top high-severity findings (`HIGH`/`ERROR`).
  - Contains per-file issue counts for warnings JSON artifacts.
  - Contains artifact-presence flags (`nlt-junit.xml`, `nlt_logs`, log-usage exports).

## Compatibility artifacts

- `nlt-junit.xml`: JUnit test-case output consumed by test-result publishers.
- `nlt-errors.json`: warnings stream for core log file errors.
- `nlt-server-leaks.json`: warnings stream focused on server leak checks.
- `nlt-client-leaks.json`: warnings stream focused on client leak checks.

Warnings JSON entries now include `runId` so findings can be correlated across all outputs from
the same NLT execution.

## Log artifacts

- `nlt_logs/*`: daemon/client/fault-injection logs and compressed `.bz2` variants.
- `dnt*.xml`: valgrind outputs where applicable.
- `nltir.xml` / `nltr.json`: optional log-usage reports when requested.

## CI consumption guidance

- Use `nlt-summary.json` as the first-stop triage surface.
- Keep consuming `nlt-junit.xml` for pass/fail test publishing.
- Keep warnings JSON consumption for backward compatibility and gating.
