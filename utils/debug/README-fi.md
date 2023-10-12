# How to add a Fault Injection test in DAOS

This guide explains, step by step, how to introduce a **fault injection (FI) point** in
the DAOS C source code and pair it with a **Python functional test** that exercises it.
Every step is illustrated with code from PR [#18489](https://github.com/daos-stack/daos/pull/18489)
(DAOS-19016), which fixed a stale-event-pointer dereference in `daos_autotest.c`.

> **About PR #18489:** The FI point and functional test introduced by this PR were
> ultimately removed following reviewer feedback — the chosen insertion point was in
> a hot path. That context makes this PR an ideal case study: the code is complete and
> correct, and the lesson learned is captured in the [Hotpath constraint](#hotpath-constraint)
> section below.

---

## Table of contents

1. [When to write a FI test](#1-when-to-write-a-fi-test)
2. [The DAOS fault-injection framework](#2-the-daos-fault-injection-framework)
3. [Hotpath constraint and mitigation](#3-hotpath-constraint-and-mitigation)
4. [Step 1 — Pick a unique FI constant ID](#step-1--pick-a-unique-fi-constant-id)
5. [Step 2 — Define the constant in `common.h`](#step-2--define-the-constant-in-commonh)
6. [Step 3 — Insert the FI check in C code](#step-3--insert-the-fi-check-in-c-code)
7. [Step 4 — Register the FI point in `fault_config_utils.py`](#step-4--register-the-fi-point-in-fault_config_utilspy)
8. [Step 5 — Write the YAML test configuration](#step-5--write-the-yaml-test-configuration)
9. [Step 6 — Write the Python functional test](#step-6--write-the-python-functional-test)
10. [Running the test](#running-the-test)
11. [Pre-submission checklist](#pre-submission-checklist)

---

## 1. When to write a FI test

A FI test is the right tool when:

- You fix a code path that handles a **transient or rare error condition** (network
  failure, allocation failure, I/O error) that is difficult to reproduce naturally in CI.
- The fix involves a **new branch** (e.g., `if (rc < 0) break;`) that would be dead code
  without a synthetic stimulus.
- You want to prove that the error propagates cleanly **all the way to the caller** with
  the correct error code and without crashing, hanging, or corrupting state.

A FI test is NOT needed when:

- The error condition can be triggered deterministically by existing test infrastructure
  (e.g., stopping a server, running out of quota).
- The code path is a hot path — see [Section 3](#3-hotpath-constraint-and-mitigation).

---

## 2. The DAOS fault-injection framework

DAOS uses the [GURT fault-inject library](https://github.com/daos-stack/daos/blob/master/src/cart/src/gurt/fault_inject.c).
The key building blocks are:

### 2.1 Fault ID layout

Every FI point is identified by a 32-bit **fault location** (`fail_loc`) with the
following bit layout (from `src/include/daos/common.h`):

```
Bits [0..15]   — per-group fault ID (16-bit offset)
Bits [16..23]  — group ID
Bits [24..31]  — fault mode flags (ONCE, SOME, ALWAYS)
```

Two groups are defined for use in functional tests:

| Constant | Value | Purpose |
|----------|-------|---------|
| `DAOS_FAIL_UNIT_TEST_GROUP` | 1 | Server-side unit tests |
| `DAOS_FAIL_SYS_TEST_GROUP` | 2 | End-to-end functional tests |

The convenience macros that incorporate the group shift:

```c
#define DAOS_FAIL_GROUP_SHIFT          16
#define DAOS_FAIL_UNIT_TEST_GROUP_LOC  (DAOS_FAIL_UNIT_TEST_GROUP << DAOS_FAIL_GROUP_SHIFT)
#define DAOS_FAIL_SYS_TEST_GROUP_LOC   (DAOS_FAIL_SYS_TEST_GROUP  << DAOS_FAIL_GROUP_SHIFT)
```

### 2.2 Key macros

```c
/* Look up the fault attribute struct for a given fault ID. Returns NULL when FI
 * is globally disabled or the ID is not registered. This call acquires a lock
 * and performs a hash-table lookup — it is NOT free. */
struct d_fault_attr_t *d_fault_attr_lookup(uint32_t fault_id);

/* Evaluate whether the fault should fire on this invocation (honors interval,
 * max_faults, probability). Returns true if the fault should be injected. */
bool D_SHOULD_FAIL(struct d_fault_attr_t *fa);

/* Global boolean: true when fault injection is compiled in and enabled at
 * runtime. False in release builds and when --fault-inject=false is passed. */
extern bool d_fault_inject;
```

### 2.3 FI is always on in non-release builds

In dev and CI builds (i.e., any build without `-DDAOS_BUILD_RELEASE=1`), the fault
injection infrastructure is compiled in and **enabled by default**. This means
`d_fault_attr_lookup()` is a real function call — with a lock acquisition — on every
code path that contains it, even when no fault is actually configured for that ID.

This has a direct consequence for where FI checks may be safely placed. See the next
section.

---

## 3. Hotpath constraint and mitigation

> **This is the most important section in this guide.** Ignoring it can cause measurable
> performance regressions in all non-release builds, not just during FI-specific tests.

### 3.1 The problem

`d_fault_attr_lookup()` is **not cheap**: it acquires a reader lock and performs a lookup
in the GURT hash table. Placing a bare FI check inside a tight loop or a frequently
polled function means this overhead is paid on every iteration of every CI test run.

From PR #18489, reviewer **mchaarawi** explicitly flagged this on the proposed FI check
inside `daos_eq_poll()`:

> *"d_fault_attr_lookup is expensive (requires acquiring lock and lookup in the gurt
> hashtable). We should not put this FI in the hotpath of eq_poll which can be called in
> a loop by application with 0 timeout. I would rather not do any FI at all in polling
> please."*
> — [r3414870102](https://github.com/daos-stack/daos/pull/18489#discussion_r3414870102)

### 3.2 The proposed mitigation

To reduce overhead in tight loops, guard the expensive lookup with the global
`d_fault_inject` boolean. When FI is globally disabled (release builds, or when the
fault-inject subsystem is off), the branch reduces to a single zero-comparison — no lock,
no hashtable:

```c
/* Fault injection: simulate crt_progress failure before dequeue. */
if (unlikely(d_fault_inject)) {
    fa = d_fault_attr_lookup(DAOS_FAULT_EQ_POLL_FAIL);
    if (fa != NULL && D_SHOULD_FAIL(fa)) {
        daos_eq_putref(epa.eqx);
        return -DER_HG;
    }
}
```

This was proposed by the author in response to the initial feedback
([r3415852837](https://github.com/daos-stack/daos/pull/18489#discussion_r3415852837)).

### 3.3 When the guard is still not enough

The reviewer's reply explained that even the `unlikely(d_fault_inject)` guard is not
always acceptable:

> *"All tests are done with FI enabled (non-release build), so not just FI stage. So
> this will be exercised in all user cases where apps or tests call poll in a loop
> anywhere we use a non-release build. For this particular case, I do not see really a
> big benefit of this FI test case to incur such an issue for non-release builds."*
> — [r3415924620](https://github.com/daos-stack/daos/pull/18489#discussion_r3415924620)

### 3.4 Decision guide

| Code path characteristic | Recommendation |
|--------------------------|---------------|
| Called at most a few times per operation (e.g., `pool_open`, `container_create`) | Bare `d_fault_attr_lookup()` is fine |
| Called in a loop but infrequently (e.g., a retry loop with a sleep) | Guard with `unlikely(d_fault_inject)` |
| Called in a tight spin loop or poll loop with 0 timeout | Avoid FI entirely; consider injecting the failure at a higher layer |

**Rule of thumb:** If your FI check would execute thousands of times per second in a
normal IOR or mdtest run, it does not belong in the hot path.

---

## Step 1 — Pick a unique FI constant ID

Open `src/include/daos/common.h` and scan the existing constants for the group you want
to use (`DAOS_FAIL_SYS_TEST_GROUP_LOC` for functional tests). Find the highest existing
offset and pick the next available value.

```
# Current high-water marks in src/include/daos/common.h (as of master at PR #18489)
DAOS_FAULT_POOL_EXT_RESERVED   = DAOS_FAIL_SYS_TEST_GROUP_LOC | 0x20a
...
# Existing constants already use 0x80-0x83, 0x100-0x10a, 0x200-0x20a
# PR #18489 chose 0x1000 to leave a large gap for future pool-related constants
DAOS_FAULT_EQ_POLL_FAIL        = DAOS_FAIL_SYS_TEST_GROUP_LOC | 0x1000
```

Choose an offset that:
- Does not collide with any existing constant.
- Groups related constants together (e.g., all `EQ`-related constants near each other).
- Leaves room for future additions in the same subsystem.

---

## Step 2 — Define the constant in `common.h`

Add the `#define` near other constants from the same subsystem. Add a short comment
block to identify the subsystem.

**Diff from PR #18489** (`src/include/daos/common.h`):

```diff
 #define DAOS_FAULT_POOL_EXT_PADDING       (DAOS_FAIL_SYS_TEST_GROUP_LOC | 0x209)
 #define DAOS_FAULT_POOL_EXT_RESERVED      (DAOS_FAIL_SYS_TEST_GROUP_LOC | 0x20a)

+/* Client code fault injection */
+#define DAOS_FAULT_EQ_POLL_FAIL            (DAOS_FAIL_SYS_TEST_GROUP_LOC | 0x1000)

 #define DAOS_DTX_SKIP_PREPARE              DAOS_DTX_SPEC_LEADER
```

### Computing the decimal fault ID

The functional test registry (Step 4) requires the **decimal** value of the fault
location. Compute it as:

```
fault_loc = (group_id << 16) | per_group_offset
          = (2 << 16) | 0x1000
          = 131072   | 4096
          = 135168
```

---

## Step 3 — Insert the FI check in C code

Add the FI check immediately before the code path you want to intercept. Follow the
[hotpath constraint guidance](#3-hotpath-constraint-and-mitigation) before choosing the
insertion point.

**Diff from PR #18489** (`src/client/api/event.c`):

```diff
 daos_eq_poll(daos_handle_t eqh, int wait_running, int64_t timeout,
              unsigned int n_events, struct daos_event **events)
 {
        struct eq_progress_arg  epa;
+       struct d_fault_attr_t  *fa;
        int                     rc;

        if (n_events == 0 || events == NULL)
```

```diff
        epa.wait_running = wait_running;
        epa.count        = 0;

+       /* Fault injection: crt_progress failure BEFORE dequeue; caller's evp remains stale. */
+       fa = d_fault_attr_lookup(DAOS_FAULT_EQ_POLL_FAIL);
+       if (fa != NULL && D_SHOULD_FAIL(fa)) {
+               daos_eq_putref(epa.eqx);
+               return -DER_HG;
+       }
+
        /* pass the timeout to crt_progress() with a conditional callback */
        rc = crt_progress_cond(epa.eqx->eqx_ctx, timeout, eq_progress_cb, &epa);
```

Notes:
- Declare `fa` at the top of the function alongside other local variables.
- Add a comment explaining what the FI simulates and why (e.g., "evp remains stale").
- Call `daos_eq_putref()` (or equivalent cleanup) before returning to release any
  resource acquired before the FI check.
- Return the same error code that the real failure path would return (`-DER_HG` here
  because the failure simulates a Mercury transport error from `crt_progress()`).

---

## Step 4 — Register the FI point in `fault_config_utils.py`

The test framework looks up FI attributes by **name** from the `FAULTS` dictionary in
`src/tests/ftest/util/fault_config_utils.py`. Add an entry for your new constant.

**Diff from PR #18489** (`src/tests/ftest/util/fault_config_utils.py`):

```diff
         'probability_y': '100',
         'interval': '1',
         'max_faults': '1'},
+    'DAOS_FAULT_EQ_POLL_FAIL': {
+        'id': '135168',
+        'probability_x': '1000',
+        'probability_y': '100',
+        'interval': '100',
+        'max_faults': '5'},
 }
```

### Field reference

| Field | Meaning |
|-------|---------|
| `id` | Decimal fault location value (computed in Step 2) |
| `probability_x` | Numerator of the injection probability |
| `probability_y` | Denominator of the injection probability |
| `interval` | Only inject on every Nth hit (1 = every hit) |
| `max_faults` | Stop injecting after this many successful injections |

**Probability:** `probability_x / probability_y` = chance of injection on a given hit.
With `1000 / 100`, the fault fires with 1000% probability — i.e., always — on hits that
pass the interval filter. Use lower values (e.g., `5 / 100`) to inject only occasionally.

**Interval + max_faults together:** In the PR example, `interval: 100` means "fire on
every 100th call", and `max_faults: 5` means "fire at most 5 times total". This prevents
a high-frequency FI from exhausting a tight loop immediately but still guarantees several
injections over the lifetime of the test.

---

## Step 5 — Write the YAML test configuration

Create `src/tests/ftest/<subsystem>/<test_name>.yaml`.

**Full content from PR #18489** (`src/tests/ftest/pool/autotest_eq_poll_fi.yaml`):

```yaml
hosts:
  test_servers: 1
  test_clients: 1
timeout: 300
setup:
  start_servers_once: False
server_config:
  name: daos_server
  engines_per_host: 1
  engines:
    0:
      storage: auto
pool:
  size: 20G
faults:
  fault_list:
    - DAOS_FAULT_EQ_POLL_FAIL
```

### Key fields

| Field | Purpose |
|-------|---------|
| `hosts.test_servers` | Number of server nodes required |
| `hosts.test_clients` | Number of client nodes required |
| `timeout` | Maximum wall-clock time for the test (seconds) |
| `setup.start_servers_once` | If `False`, restart servers between test methods |
| `pool.size` | Pool size for auto-created pools |
| `faults.fault_list` | List of FI names (must match keys in `fault_config_utils.py`) |

### Sizing the timeout

Measure the actual wall-clock time of several CI runs and apply a **2× to 2.5× safety
factor** over the observed maximum. For PR #18489, all five CI runs completed in ~120 s,
giving a 300 s timeout (2.5×). Avoid arbitrary timeouts such as 600 s or 3600 s; they
delay failure detection and waste CI resources.

---

## Step 6 — Write the Python functional test

Create `src/tests/ftest/<subsystem>/<test_name>.py`.

**Full content from PR #18489** (`src/tests/ftest/pool/autotest_eq_poll_fi.py`):

```python
"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers


class PoolAutotestEqPollFITest(TestWithServers):
    """Test daos pool autotest robustness under daos_eq_poll() fault injection.

    Validates the DAOS-19016 fix: the kv_put() and kv_get() spin loops in
    src/utils/daos_autotest.c must handle daos_eq_poll() returning a negative
    error code without dereferencing the stale event pointer (evp).

    Fault injection point DAOS_FAULT_EQ_POLL_FAIL (ID 135168) injects a
    -DER_HG return from daos_eq_poll(), exercising the rc < 0 break added by
    the fix.  The expected outcome is:
      - daos pool autotest exits with rc == 1 (no crash or hang)
      - the error message contains DER_HG(-1020)

    :avocado: recursive
    """

    def test_pool_autotest_eq_poll_fi(self):
        """Test that daos pool autotest handles daos_eq_poll() errors correctly.

        Run daos pool autotest with fault injection point DAOS_FAULT_EQ_POLL_FAIL
        (fault ID 135168, enabled via the YAML faults section) active.  Confirm
        that when daos_eq_poll() returns -DER_HG the autotest exits cleanly with
        rc == 1 and reports DER_HG(-1020), proving that the stale event pointer
        fix from DAOS-19016 is working.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=pool,daos_cmd,autotest,fault_injection
        :avocado: tags=test_pool_autotest_eq_poll_fi,PoolAutotestEqPollFITest
        """
        self.log_step("Create a pool")
        self.add_pool()
        self.pool.set_query_data()
        daos_cmd = self.get_daos_command()

        # Fault injection is enabled via the YAML 'fault_list' section.
        # The autotest is expected to fail: disable the exception so that the
        # CmdResult can be inspected for the expected error signature.
        self.log_step("Run pool autotest with daos_eq_poll fault injection (DAOS-19016)")
        daos_cmd.exit_status_exception = False
        result = daos_cmd.pool_autotest(pool=self.pool.identifier)

        self.log_step("Verify autotest exited with the expected error code")
        if result.exit_status == 0:
            self.fail(
                "daos pool autotest succeeded unexpectedly; "
                "expected it to fail due to DAOS_FAULT_EQ_POLL_FAIL injection")
        if result.exit_status != 1:
            self.fail(
                f"Expected exit code 1, got {result.exit_status}; "
                f"stderr: {result.stderr_text}")

        self.log_step("Verify DER_HG(-1020) error in autotest output")
        if "DER_HG(-1020)" not in result.stderr_text:
            self.fail(
                f"Expected 'DER_HG(-1020)' in autotest stderr; "
                f"got: {result.stderr_text}")
        self.log.info(
            "Fault injection correctly propagated DER_HG(-1020) "
            "without stale event pointer dereference")

        self.log_step("Confirm pool is still healthy after the expected autotest failure")
        self.pool.set_query_data()
```

### Avocado tag conventions

| Tag | Meaning |
|-----|---------|
| `all` | Always include — required for CI selection |
| `full_regression` | Run this test in the full regression stage |
| `hw,medium` | Requires hardware nodes; `medium` = 2-node allocation |
| Module tags | Subsystem keywords that allow targeted test selection (e.g., `pool`, `fault_injection`) |
| Class tag | Exact class name — required for `avocado list` filtering |
| Method tag | Exact method name — required for `avocado run` by method |

### Structure guidelines

- Use `self.log_step("…")` at the start of each logical phase. This produces structured
  log output that makes CI failure diagnosis much faster.
- When a command is **expected to fail**, disable the automatic exception by setting
  `daos_cmd.exit_status_exception = False` before the call, then inspect
  `result.exit_status` and `result.stderr_text` manually.
- Always verify the **error code and error message** explicitly. A crash and a clean
  failure both produce a non-zero exit code; the distinction is in the output.
- After the injected failure, verify that the pool (or other shared resource) remains
  in a healthy state. This confirms the fix does not corrupt state beyond the expected
  error path.

---

## Running the test

### Via `launch.py`

```bash
# From the test client node, run the specific test method:
./src/tests/ftest/launch.py \
    -ts <server_nodes> -tc <client_node> \
    pool/autotest_eq_poll_fi.py:PoolAutotestEqPollFITest.test_pool_autotest_eq_poll_fi
```

### Via avocado tags

```bash
avocado run --filter-by-tags-expr \
    'PoolAutotestEqPollFITest and test_pool_autotest_eq_poll_fi' \
    src/tests/ftest/pool/autotest_eq_poll_fi.py
```

### Confirming FI fires

Search the test log for the injected error:

```bash
grep "DER_HG\|DAOS_FAULT_EQ_POLL_FAIL\|fault" daos_client.log | head -30
```

---

## Pre-submission checklist

Before opening the PR, verify:

- [ ] **No hotpath:** The FI insertion point is not in a tight loop or polling function.
      If it is, apply the `unlikely(d_fault_inject)` guard or move the FI to a higher
      layer. If even the guard is not acceptable (reviewer judgment), remove the FI test.
- [ ] **Unique constant ID:** The `0xXXXX` offset in `common.h` does not collide with any
      existing constant in the same group.
- [ ] **Correct decimal ID:** The `id` field in `fault_config_utils.py` matches
      `(group_id << 16) | offset`.
- [ ] **Cleanup on FI return:** All resources acquired before the FI check are released
      before returning the injected error code.
- [ ] **YAML `fault_list` name matches registry key** in `fault_config_utils.py`.
- [ ] **Test verifies both exit code and error message.** A crash and a clean failure
      look the same to the exit-status check; the message check distinguishes them.
- [ ] **Pool (or shared resource) health confirmed** after the expected failure.
- [ ] **Timeout is evidence-based:** Measured from at least three CI runs, then
      multiplied by 2× to 2.5×.
- [ ] **Avocado tags include** `all`, a stage tag (`full_regression`), a hardware tag,
      module tags, and exact class + method name tags.
