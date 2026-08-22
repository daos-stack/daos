# Node Local Test (NLT)

NLT runs a full DAOS server, client, DFuse and fault-injection stack on a **single node
over tmpfs**. It is the fastest way to smoke-test client/DFuse changes locally and is the
basis for two CI stages. The entry point is
[`utils/node_local_test.py`](../../../utils/node_local_test.py), a thin wrapper around this
`nlt` package.

## Running

```bash
# Full CI POSIX/DFuse suite (what the "NLT" CI stage runs)
./utils/node_local_test.py --memcheck no all

# A single test (see the full list, POSIX + manual, with --test list)
./utils/node_local_test.py --test list
./utils/node_local_test.py --test cont_copy

# Fault-injection suite (what the "Fault injection testing" CI stage runs)
./utils/node_local_test.py --memcheck no fi

# Drop into a shell with DFuse mounted
./utils/node_local_test.py launch bash
```

`python -m nlt ...` works too.

### Suites

Tests too slow or too disk-hungry for CI are kept out of the default run and only execute
when explicitly requested:

| `--suite`  | POSIX tests            | Fault-injection tests                         |
| ---------- | ---------------------- | --------------------------------------------- |
| `ci` (def) | `test_*` methods       | the standard FI set                           |
| `manual`   | `manual_*` methods     | `test_dfs_check`, `test_alloc_pil4dfs_ls`     |
| `all`      | both                   | both                                          |

Manual POSIX tests are methods named `manual_*` on `PosixTests`; add one and it becomes
available under `--suite manual|all` without touching the CI path. The server-only
probabilistic fault test is separate: `--server-fi`.

## Reading the results

NLT writes one **human-readable summary** to `nlt-summary.md` (or `nlt-summary-<class-name>.md`
when `--class-name` is set, so parallel CI stages do not overwrite each other; also printed to
the console and archived by CI). Read this first — it consolidates everything below into a
single file so you rarely need to open the raw logs:

- overall verdict and pass/fail/finding counts;
- each **failed test** with the DFuse/DAOS **log lines that caused it** quoted inline, with a
  `logfile:line` reference;
- remaining log-analysis findings grouped per test, plus a `server-wide / shared logs` group
  for findings from the shared server log;
- valgrind/memcheck notes and the slowest tests.

Disable it with `--summary ""`.

The machine-readable artifacts are still produced for the Jenkins plugins:

| File                     | Contents                                             |
| ------------------------ | ---------------------------------------------------- |
| `nlt-junit.xml`          | JUnit results (per test case)                        |
| `nlt-errors.json`        | log-analysis findings (warnings-ng plugin)           |
| `nlt-server-leaks.json`  | server memory-leak findings                          |
| `nlt-client-leaks.json`  | client leak findings (FI runs)                       |
| `dnt.*.memcheck.xml`     | valgrind output                                      |
| `nlt_logs/<class>/`      | raw `dnt*.log` DAOS/DFuse logs                        |

By default only logs that produced a finding are kept (and fault-injection keeps only the
iterations that crashed or reported something); the many clean per-command, DFuse and
fault-location logs are dropped after analysis. Pass `--keep-logs` to retain every log.

## Package layout

The former ~7k-line script is split into a dependency-ordered package (low level first):

| Module                | Responsibility                                                        |
| --------------------- | -------------------------------------------------------------------- |
| `base.py`             | exceptions, ids, per-thread active-test context, timers, ratchet     |
| `watchdog.py`         | FUSE-wedge watchdog (`WedgeWatch`) and stall diagnostics             |
| `config.py`           | build config (`NLTConf`) and environment helpers                     |
| `reporting.py`        | `WarningsFactory`: JUnit/JSON output and the `nlt-summary.md` report  |
| `logging_utils.py`    | output capture and DAOS log-file analysis (`log_test`)               |
| `client.py`           | daos command helpers, pools, containers, valgrind wrapper            |
| `server.py`           | `DaosServer` single-node server management                           |
| `dfuse.py`            | `DFuse` mounts and the `needs_dfuse*` test decorators                |
| `helpers.py`          | shared DFuse/IL test helpers                                          |
| `posix_tests.py`      | `PosixTests` and the parallel POSIX/DFuse test runner                |
| `fault_injection.py`  | client fault-injection tests                                          |
| `special_tests.py`    | multi-mount, overlay, pydaos and perf tests                          |
| `runner.py`           | test selection and top-level orchestration (`run`)                  |
| `cli.py`              | argument parsing and `main`                                          |

## How findings are correlated to tests

POSIX tests run in parallel threads, each with its own container and DFuse instance.
`base.set_active_test()` records the running test per-thread; `WarningsFactory.add()` tags
every log-analysis finding with it, which is what lets the summary place a finding next to
the test that produced it. Findings from the shared server log (analyzed once at shutdown)
have no owning test and appear under `server-wide / shared logs`.
