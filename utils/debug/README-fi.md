# How to add a Fault Injection test in DAOS

This guide explains, step by step, how to introduce a **fault injection (FI) point** in the DAOS C source code and pair it with a test: either a **Python functional test** (Steps 4-6) or a **C unit test** (see [Pairing the FI point with a C unit test](#pairing-the-fi-point-with-a-c-unit-test)). Every Python ftest step is illustrated with code from PR [#18489](https://github.com/daos-stack/daos/pull/18489) (DAOS-19016), which fixed a stale-event-pointer dereference in `daos_autotest.c`.

> **About PR #18489:** The FI point and functional test introduced by this PR were ultimately removed following reviewer feedback — the chosen insertion point was in a hot path. That context makes this PR an ideal case study: the code is complete and correct, and the lesson learned is captured in the [Hotpath constraint](#3-hotpath-constraint-and-mitigation) section below.

---

## Table of contents

1. [When to write a FI test](#1-when-to-write-a-fi-test)
2. [The DAOS fault-injection framework](#2-the-daos-fault-injection-framework)
3. [Hotpath constraint and mitigation](#3-hotpath-constraint-and-mitigation)
4. [Step 1 — Pick a unique FI constant ID](#step-1--pick-a-unique-fi-constant-id)
5. [Step 2 — Define the constant in `common.h`](#step-2--define-the-constant-in-commonh)
6. [Step 3 — Insert the FI check in C code](#step-3--insert-the-fi-check-in-c-code)
7. [Pairing the FI point with a C unit test](#pairing-the-fi-point-with-a-c-unit-test)
8. [Step 4 — Register the FI point in `fault_config_utils.py`](#step-4--register-the-fi-point-in-fault_config_utilspy)
9. [Step 5 — Write the YAML test configuration](#step-5--write-the-yaml-test-configuration)
10. [Step 6 — Write the Python functional test](#step-6--write-the-python-functional-test)
11. [Running the test](#running-the-test)
12. [Pre-submission checklist](#pre-submission-checklist)

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

DAOS uses the [GURT fault-inject library](https://github.com/daos-stack/daos/blob/master/src/gurt/fault_inject.c). The framework has two commonly used layers: DAOS fail-location helpers in `src/common/fail_loc.c` and the lower-level GURT attribute table in `src/gurt/fault_inject.c`.

### 2.1 Fault ID layout

Every DAOS fail-location value is stored in the process-global `uint64_t daos_fail_loc` (`src/common/fail_loc.c: daos_fail_loc`) with the layout defined in `src/include/daos/common.h`:

```c
#define DAOS_FAIL_MASK_LOC    0x000000ffff /* bits 0-15: per-group offset */
#define DAOS_FAIL_GROUP_SHIFT 16
#define DAOS_FAIL_GROUP_MASK  0xff0000     /* bits 16-23: group */
#define DAOS_FAIL_ID_MASK     0xffffff     /* bits 0-23: group + offset */
#define DAOS_FAIL_ONCE        0x1000000    /* bit 24 */
#define DAOS_FAIL_SOME        0x2000000    /* bit 25 */
#define DAOS_FAIL_ALWAYS      0x4000000    /* bit 26 */
```

Two groups are defined for DAOS fail-location IDs:

| Constant | Value | Purpose |
|----------|-------|---------|
| `DAOS_FAIL_UNIT_TEST_GROUP` | 1 | C unit, VOS, and suite tests, plus the vast majority of the ftest registry entries |
| `DAOS_FAIL_SYS_TEST_GROUP` | 2 | YAML-driven functional faults and tool faults such as dlck and the ddb hook proposed in PR #18941 |

The convenience macros that incorporate the group shift:

```c
#define DAOS_FAIL_GROUP_SHIFT          16
#define DAOS_FAIL_UNIT_TEST_GROUP_LOC  (DAOS_FAIL_UNIT_TEST_GROUP << DAOS_FAIL_GROUP_SHIFT)
#define DAOS_FAIL_SYS_TEST_GROUP_LOC   (DAOS_FAIL_SYS_TEST_GROUP  << DAOS_FAIL_GROUP_SHIFT)
```

Either group can be armed from C: `src/common/fail_loc.c: daos_fail_loc_set()` registers or updates the attribute under the group extracted from the ID when a mode bit is present, then stores the full location and mode bits in `daos_fail_loc`.

### 2.2 Key macros and idioms

```c
/* Runtime predicate for DAOS fail-location IDs. */
#define DAOS_FAIL_CHECK(id) daos_fail_check(id)

/* Look up the fault attribute struct for a given fault ID. Returns NULL when the
 * id is not registered, or always in release builds. This call acquires a lock
 * and performs a hash-table lookup — it is NOT free. */
struct d_fault_attr_t *d_fault_attr_lookup(uint32_t fault_id);

/* Evaluate whether the fault should fire on this invocation. Expands to
 * d_fault_inject && d_should_fail(fa), so it already tests the runtime flag. */
bool D_SHOULD_FAIL(struct d_fault_attr_t *fa);

/* Global on/off switch: 0 unless a D_FI_CONFIG file was parsed successfully or
 * d_fault_inject_enable() was called after a config file was loaded. */
extern unsigned int d_fault_inject;
```

There are two DAOS FI idioms, and they have different costs and arming paths:

| Idiom | Use it when | Runtime path | Usage on master |
|-------|-------------|--------------|-----------------|
| `DAOS_FAIL_CHECK(id)` | A DAOS fail-location ID should be armed from C tests, server RPC parameters, or `D_FI_CONFIG` | `src/include/daos/common.h: DAOS_FAIL_CHECK()` calls `src/common/fail_loc.c: daos_fail_check()` | Dominant idiom: about 230 call sites under `src/` at the time of writing, plus the ddb hook proposed in PR #18941 |
| `d_fault_attr_lookup(id)` + `D_SHOULD_FAIL(fa)` | A YAML-only GURT attribute is enough and the call site is not hot | `src/gurt/fault_inject.c: d_fault_attr_lookup()` takes the RDLOCK and hashes the ID, then `src/include/gurt/fault_inject.h: D_SHOULD_FAIL()` gates `d_should_fail()` on `d_fault_inject` | Four production sites outside the framework/tests: `src/cart/crt_swim.c`, `src/client/api/init.c` (two sites), and `src/client/dfuse/dfuse_main.c` |

`DAOS_FAIL_CHECK(id)` merges the process-global location with YAML attributes. `src/common/fail_loc.c: daos_fail_check()` returns immediately when no low-16-bit match is armed and no YAML config is enabled; otherwise it looks up the full 24-bit ID when `D_FI_CONFIG` is enabled, falls back to the group attribute, and then calls `src/gurt/fault_inject.c: d_should_fail()` to apply thread enablement, probability, interval, `fa_max_faults`, and counters.

C tests arm the process-global location with `src/common/fail_loc.c: daos_fail_loc_set()`, `daos_fail_value_set()`, and `daos_fail_num_set()`. Client-side C tests can arm an engine by calling `src/include/daos_mgmt.h: daos_debug_set_params()` with `DMG_KEY_FAIL_LOC`, `DMG_KEY_FAIL_VALUE`, or `DMG_KEY_FAIL_NUM`; the server receives that through `src/engine/srv.c: dss_parameters_set()` and calls the same setters.

A direct `d_fault_attr_lookup()` never checks `d_fault_inject`: in non-release builds it always takes the RDLOCK and performs the hash lookup, returning `NULL` only when the ID is not registered; in release builds the `#else` stub in `src/gurt/fault_inject.c: d_fault_attr_lookup()` returns `NULL`.

### 2.3 FI is compiled in for non-release builds and enabled by config

`scons BUILD_TYPE=release` defines `DAOS_BUILD_RELEASE` and compiles out `FAULT_INJECTION`; every other `BUILD_TYPE` defines `FAULT_INJECTION=1` (`site_scons/site_tools/compiler_setup.py`). Compiled in does not mean enabled: `src/gurt/fault_inject.c: d_fault_inject_init()` leaves `d_fault_inject == 0` when `D_FI_CONFIG` is absent, sets it to `1` only after a parsable YAML file is loaded, and `d_fault_inject_enable()` returns `-DER_NOSYS` if no config file was loaded.

Python ftests that declare a `faults:` block get a generated `D_FI_CONFIG`: `src/tests/ftest/util/fault_config_utils.py` writes the YAML file and `src/tests/ftest/util/server_utils_params.py` exports it to every server. The ftest registry currently has 36 IDs: 35 in the unit-test group and 1 in the system-test group.

This distinction matters for cost: a direct GURT lookup costs an unconditional lock and hash in every non-release build, while either idiom may also pay per-call lookup and `d_should_fail()` costs in FI-enabled runs.

---

## 3. Hotpath constraint and mitigation

> **This is the most important section in this guide.** Ignoring it can cause measurable performance regressions in non-release builds and in FI-enabled test runs.

### 3.1 The problem

`d_fault_attr_lookup()` is **not cheap**: it acquires a reader lock and performs a lookup in the GURT hash table. Placing a direct YAML-only FI check inside a tight loop or a frequently polled function means this overhead is paid on every iteration in every non-release build, even when no fault is enabled for that ID.

From PR #18489, reviewer **mchaarawi** explicitly flagged this on the proposed FI check inside `daos_eq_poll()`:

> *"d_fault_attr_lookup is expensive (requires acquiring lock and lookup in the gurt
> hashtable). We should not put this FI in the hotpath of eq_poll which can be called in
> a loop by application with 0 timeout. I would rather not do any FI at all in polling
> please."*
> — [r3414870102](https://github.com/daos-stack/daos/pull/18489#discussion_r3414870102)

### 3.2 The proposed mitigation

For the direct `d_fault_attr_lookup()` idiom only, an `unlikely(d_fault_inject)` guard can spare the lock and hash lookup when no YAML config has enabled FI. The guard does not make `D_SHOULD_FAIL()` cheaper, because `src/include/gurt/fault_inject.h: D_SHOULD_FAIL()` already expands to `d_fault_inject && d_should_fail(fa)`.

```c
/* Historical PR #18489 example: simulate crt_progress failure before dequeue. */
if (unlikely(d_fault_inject)) {
    fa = d_fault_attr_lookup(DAOS_FAULT_EQ_POLL_FAIL);
    if (fa != NULL && D_SHOULD_FAIL(fa)) {
        daos_eq_putref(epa.eqx);
        return -DER_HG;
    }
}
```

This was proposed by the author in response to the initial feedback on the historical PR #18489 `DAOS_FAULT_EQ_POLL_FAIL` check ([r3415852837](https://github.com/daos-stack/daos/pull/18489#discussion_r3415852837)).

### 3.3 When the guard is still not enough

The reviewer's reply explained that even the `unlikely(d_fault_inject)` guard is not always acceptable:

> *"All tests are done with FI enabled (non-release build), so not just FI stage. So
> this will be exercised in all user cases where apps or tests call poll in a loop
> anywhere we use a non-release build. For this particular case, I do not see really a
> big benefit of this FI test case to incur such an issue for non-release builds."*
> — [r3415924620](https://github.com/daos-stack/daos/pull/18489#discussion_r3415924620)

For current code, read that cost as two related hazards: a direct lookup is an unconditional lock and hash in every non-release build, and once a YAML config is loaded either idiom can do per-call attribute lookup and `d_should_fail()` work. `DAOS_FAIL_CHECK()` has a cheap built-in fast path when nothing is armed and no YAML is loaded, but after `D_FI_CONFIG` is active it can look up the full ID and then the group attribute on each call.

### 3.4 Decision guide

| Code path characteristic | Recommendation |
|--------------------------|---------------|
| Called at most a few times per operation (e.g., `pool_open`, `container_create`) | `DAOS_FAIL_CHECK(id)` is usually preferred; a direct `d_fault_attr_lookup()` is acceptable for YAML-only faults |
| Called in a loop but infrequently (e.g., a retry loop with a sleep) | Prefer `DAOS_FAIL_CHECK(id)` for its cheap no-FI fast path; if using direct lookup, guard it with `unlikely(d_fault_inject)` |
| Called in a tight spin loop or poll loop with 0 timeout | Avoid FI entirely; consider injecting the failure at a higher layer or in a C unit test that forges inputs before an existing check |

**Rule of thumb:** If your FI check would execute thousands of times per second in a normal IOR or mdtest run, it does not belong in the hot path.

---

## Step 1 — Pick a unique FI constant ID

Open `src/include/daos/common.h` and scan **both** DAOS fail-location groups, not just the group you expect to use. `src/common/fail_loc.c: daos_fail_check()` first compares only the low 16 bits (`DAOS_FAIL_MASK_LOC`) of the armed `daos_fail_loc` and the call-site ID, so an armed `DAOS_FAIL_SYS_TEST_GROUP_LOC | 0x103` reaches a `DAOS_FAIL_CHECK(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x103)` call before the later attribute lookup decides whether the fault fires.

The tree already has a low-16-bit overlap: unit-group `DAOS_WAL_NO_REPLAY`, `DAOS_WAL_FAIL_REPLAY`, and `DAOS_MEM_FAIL_CHECKPOINT` use offsets `0x100-0x102` and are armed from C in `src/vos/tests/vts_wal.c`, while system-group `DLCK_MOCK_ROOT`, `DLCK_FAULT_GETGRNAM`, and `DLCK_MOCK_NO_DAOS_SERVER_GROUP` use offsets `0x100-0x102` and are YAML-driven by `src/utils/dlck/tests/fault_injection_dlck.yaml`. That overlap is harmless only because those faults are not armed in the same process.

```
# Current offsets in src/include/daos/common.h
# Unit-test group high-water mark: 0x102 (WAL/checkpoint faults)
# System-test group: 0x80-0x83, 0x100-0x10a, 0x200-0x20a
# Proposed in PR #18941: DDB_CSUM_NR_INJECT = DAOS_FAIL_SYS_TEST_GROUP_LOC | 0x400
# Historical PR #18489 example, no longer present on master: DAOS_FAULT_EQ_POLL_FAIL = DAOS_FAIL_SYS_TEST_GROUP_LOC | 0x1000
```

Choose an offset that:
- Does not collide with any existing constant in either group when only the low 16 bits are compared.
- Groups related constants together (e.g., all dlck-related constants near each other).
- Leaves room for future additions in the same subsystem.

`src/common/fail_loc.c: daos_fail_loc_set()` accepts IDs from either group, so group choice is about convention and YAML drivability, not about whether a C test can arm the fault.

---

## Step 2 — Define the constant in `common.h`

Add the `#define` near other constants from the same subsystem. Add a short comment block to identify the subsystem. All `DAOS_FAULT_EQ_POLL_FAIL` snippets in this guide are historical PR #18489 examples; that constant is no longer present on master.

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

Add the FI check immediately before the code path you want to intercept. Follow the [hotpath constraint guidance](#3-hotpath-constraint-and-mitigation) before choosing the insertion point.

There are two useful injection styles. The first is to return the same error code that the real failure path would return, as the historical PR #18489 ftest example did for `DAOS_FAULT_EQ_POLL_FAIL` and `-DER_HG`. The second is input forging: alter the input to an existing check so the real error branch runs, producing the real `D_ERROR` and following the real cleanup labels; this is preferable when that guard is the fix you added, as in the ddb checksum-count hook proposed in PR #18941.

Parameter channels depend on the arming path. `src/common/fail_loc.c: daos_fail_value_set()`, `daos_fail_value_get()`, and `daos_fail_num_set()` are C/RPC-only and cannot be set from a YAML `D_FI_CONFIG`, so `daos_fail_value_get()` returns 0 in a purely YAML-driven run unless C/RPC code set it. YAML provides `err_code` via `src/include/gurt/fault_inject.h: d_fault_attr_err_code()` and stores `argument` in `struct d_fault_attr_t.fa_argument`, read from the attribute returned by `d_fault_attr_lookup()` (there is no dedicated accessor). A hook meant to work from both C and YAML should not rely on `daos_fail_value` alone; for example, fall back to a fixed perturbation such as `csum_nr + 1` when the value is 0.

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
- Declare `fa` at the top of the function alongside other local variables when using the direct GURT lookup idiom.
- Add a comment explaining what the FI simulates and why (e.g., "evp remains stale").
- Call `daos_eq_putref()` (or equivalent cleanup) before returning to release any resource acquired before the FI check.
- Return the same error code that the real failure path would return, or forge input so the real branch returns it after running the normal cleanup path.

---

## Pairing the FI point with a C unit test

Use this path when a cmocka test can exercise the branch directly. DAOS test binaries get FI initialized for free because `src/common/debug.c: daos_debug_init()` calls `src/common/fail_loc.c: daos_fail_init()`, which calls `d_fault_inject_init()` and registers the unit-test group attribute.

Guard each FI-dependent test so release builds skip cleanly. Suites that include `src/vos/tests/vts_common.h: FAULT_INJECTION_REQUIRED()` can reuse it; otherwise use a file-local copy like the ddb tests proposed in PR #18941:

```c
/* Tests relying on DAOS fault injection cannot run on builds that compile it out (release) */
#if FAULT_INJECTION
#define FAULT_INJECTION_REQUIRED()                                                                 \
	do {                                                                                       \
	} while (0)
#else
#define FAULT_INJECTION_REQUIRED()                                                                 \
	do {                                                                                       \
		print_message("Skip test %s(): Fault injection required\n", __func__);             \
		skip();                                                                            \
	} while (0)
#endif /* FAULT_INJECTION */
```

Arm the fault immediately before the call under test. If the hook reads a value, set that first; then use `DAOS_FAIL_ONCE` for the next hit, `DAOS_FAIL_SOME` plus `daos_fail_num_set()` for N hits, or `DAOS_FAIL_ALWAYS` for an unlimited fault:

```c
static void
csum_nr_inject(uint32_t csum_nr)
{
	daos_fail_value_set(csum_nr);
	daos_fail_loc_set(DDB_CSUM_NR_INJECT | DAOS_FAIL_ONCE);
}
```

Assert both the return code and the side effect you are preventing. For callbacks, use a callback that calls `fail_msg()` if invoked, then add a control step with a consistent injected value to prove the hook itself is behaviour-neutral.

Use dedicated setup and teardown fixtures that reset the value and location, then chain the suite's normal fixtures; `src/vos/tests/vts_gc.c` and `src/tests/suite/daos_test.h` are precedents, and the ddb tests proposed in PR #18941 use `TEST_CSUM_FI()` to pair `dv_test_csum_fi_setup()` with `dv_test_csum_fi_teardown()`:

```c
static int
dv_test_csum_fi_setup(void **state)
{
	daos_fail_value_set(0);
	daos_fail_loc_reset();
	return dv_test_csum_setup(state);
}

static int
dv_test_csum_fi_teardown(void **state)
{
	daos_fail_value_set(0);
	daos_fail_loc_reset();
	return dv_test_csum_teardown(state);
}
```

Use both fixtures. The teardown is the only code guaranteed to run after a failed or skipped test body (cmocka runs it after a pass, a failure or a skip once setup succeeded), so it is what keeps an armed location from leaking into the next test that reaches the same hook; the setup protects the suite's own fixture and pre-arm code from a location leaked by an unrelated earlier test that forgot to disarm. `src/common/fail_loc.c: daos_fail_loc_reset()` only resets `daos_fail_loc` through `daos_fail_loc_set(0)` and does not clear `daos_fail_value` or `daos_fail_num`, so reset the value explicitly.

Either group can be armed from C. The unit-test group is the majority convention, while the system-test group keeps an ID drivable from YAML; dlck uses that path today, and the ddb `DDB_CSUM_NR_INJECT` hook is proposed in PR #18941 as a system-test-group tool fault.

Worked example: PR #18941 proposes `DDB_CSUM_NR_INJECT` in `src/include/daos/common.h` and four `DAOS_FAIL_CHECK(DDB_CSUM_NR_INJECT)` hooks in `src/utils/ddb/ddb_vos.c: dump_csum_sv()`, `dump_csum_recx()`, `check_csum_sv()`, and `check_csum_recx()`. The hooks read the real checksum-info count after `vos_ioh2ci()`, optionally forge it with `daos_fail_value_get()`, and then let the newly added consistency checks return `-DER_CSUM` through the normal cleanup labels.

The corresponding cmocka tests in `src/utils/ddb/tests/ddb_vos_tests.c` are `dump_csum_sv_inconsistent_tests`, `dump_csum_recx_inconsistent_tests`, `check_csum_sv_inconsistent_tests`, and `check_csum_recx_inconsistent_tests`. They skip with `FAULT_INJECTION_REQUIRED()` on release builds, inject inconsistent counts, assert `-DER_CSUM`, use `dump_cb_unexpected()` or `check_cb_unexpected()` to fail if the callback runs, and then inject a consistent count as a control. In FI-enabled unit-test builds, each injected inconsistent value produces one `D_ERROR` line such as `inconsistent checksum metadata (expected 0 or 1, got 2)`.

---

## Step 4 — Register the FI point in `fault_config_utils.py`

> **Ftest path only:** Steps 4-6 apply when the FI point is paired with a Python functional test; skip them for a pure C unit test.

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

> **Ftest path only:** This YAML file is how Python ftests request a generated `D_FI_CONFIG` for servers.

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

> **Ftest path only:** This section is the Python functional-test counterpart to the C unit-test section above.

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

Search the test log for the injected error from the historical PR #18489 ftest example:

```bash
grep "DER_HG\|DAOS_FAULT_EQ_POLL_FAIL\|fault" daos_client.log | head -30
```

### Where FI tests run in CI

C unit suites listed in `utils/utest.yaml` run in Jenkins `Unit Test` and `Unit Test with memcheck` stages through `ci/unit/test_main.sh`, `ci/unit/test_main_node.sh`, and `utils/run_utest.py --sudo=no` with `--memcheck` when `WITH_VALGRIND=memcheck`. The ddb suite is one of those entries (`bin/ddb_tests` and `bin/ddb_ut`).

Those stages use the `Build on EL 9` artifacts, and the Jenkinsfile gets its default scons arguments from the shared pipeline library's `sconsFaultsArgs()`, which normally yields `BUILD_TYPE=dev` unless the run is a release candidate, the commit message carries a `faults-enabled: false` pragma, or the `BuildType` parameter overrides it. Because `BUILD_TYPE=dev` defines `FAULT_INJECTION=1`, tests protected by `FAULT_INJECTION_REQUIRED()` execute in PR unit-test builds and skip only when FI is compiled out.

The Jenkins `Fault injection testing` stage is NLT-based and is explicitly skipped for release builds. Python ftest FI cases still need the `faults:` YAML block from Step 5 so servers receive `D_FI_CONFIG`; compiling FI into the build is not enough to set `d_fault_inject`.

No GitHub Actions workflow currently runs DAOS unit or FI tests automatically: `.github/workflows/unit-testing.yml` is `workflow_dispatch`-only, and `utils/run_utest.py --gha` selects only suites marked `gha: True` in `utils/utest.yaml`.

---

## Pre-submission checklist

Before opening the PR, verify:

- [ ] **No hotpath:** The FI insertion point is not in a tight loop or polling function. If it is, prefer moving the FI to a higher layer or a C unit test; for direct `d_fault_attr_lookup()` only, an `unlikely(d_fault_inject)` guard can spare the disabled-case lookup, but reviewer judgment may still require removing the FI.
- [ ] **Unique constant ID:** The `0xXXXX` offset in `common.h` does not collide with any existing constant in **either** group when compared on the low 16 bits.
- [ ] **Correct decimal ID:** For an ftest registry entry, the `id` field in `fault_config_utils.py` matches `(group_id << 16) | offset`.
- [ ] **Cleanup on FI return:** All resources acquired before the FI check are released before returning the injected error code, or the hook forges input so the real cleanup path runs.
- [ ] **Parameter channel matches the arming path:** Use `daos_fail_value`/`daos_fail_num` only for C or RPC arming, and use YAML `err_code`/`argument` attributes for `D_FI_CONFIG`-driven faults.
- [ ] **C unit test skips cleanly when FI is compiled out:** Each FI-dependent cmocka test calls `FAULT_INJECTION_REQUIRED()` before arming or asserting the injected path.
- [ ] **C unit test resets value and location in setup and teardown:** Pair `daos_fail_value_set(0)` with `daos_fail_loc_reset()` before and after the test body.
- [ ] **Hook cannot fire in release builds:** The code is under the normal DAOS FI macros/helpers, whose release-build stubs do not inject.
- [ ] **YAML `fault_list` name matches registry key** in `fault_config_utils.py` for Python ftests.
- [ ] **Test verifies both exit code and error message** for Python ftests, or both return code and side effect/callback behavior for C unit tests.
- [ ] **Pool (or shared resource) health confirmed** after the expected Python ftest failure when the test uses shared DAOS resources.
- [ ] **Timeout is evidence-based:** Measured from at least three CI runs, then multiplied by 2× to 2.5×.
- [ ] **Avocado tags include** `all`, a stage tag (`full_regression`), a hardware tag, module tags, and exact class + method name tags for Python ftests.
