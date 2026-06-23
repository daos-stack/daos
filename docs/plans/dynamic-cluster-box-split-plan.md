## Plan: Dynamic Cluster Box Split

The split logic should move into [daos/Jenkinsfile](daos/Jenkinsfile), but a small DAOS test-tag normalization plus two pipeline-lib changes are still needed. First, convert DAOS ftest source tags from `cb_01`/`cb_02`/`cb_03` to one common `cb` tag so discovery and scheduling use one unified cluster-box tag in source. Second, today [pipeline-lib/vars/getFunctionalTestStage.groovy](pipeline-lib/vars/getFunctionalTestStage.groovy) can only rebuild its own tag expression from stage_tags/default_tags; it cannot accept a precomputed explicit list of test files, and that is what the Jenkinsfile needs to pass after it splits the cb seed set. Third, the launch.py discovery logic currently lives inside [pipeline-lib/vars/testsInStage.groovy](pipeline-lib/vars/testsInStage.groovy) as a boolean-only check, so the shared discovery path should be moved into a new helper that returns the ordered list of matching tests and can also be reused by testsInStage. The same helper should optionally return ordered timeout tuples for each discovered test and support sorting by descending timeout values.

**Steps**
1. In DAOS ftest source under [daos/src/tests/ftest](daos/src/tests/ftest), convert avocado stage tags from `cb_01`/`cb_02`/`cb_03` to `cb`.
2. In [pipeline-lib/vars/getTestsInStage.groovy](pipeline-lib/vars/getTestsInStage.groovy), add a new helper that runs list_tests.py or launch.py --list for a supplied test tag and returns the ordered list of matching test files.
3. In [pipeline-lib/vars/testsInStage.groovy](pipeline-lib/vars/testsInStage.groovy), replace the inline launch.py execution with a call to getTestsInStage and keep its current public contract as a boolean wrapper.
4. Preserve the conservative skip behavior in testsInStage: if shared discovery fails, treat that as "tests found" for skip-gating purposes rather than accidentally skipping the stage.
5. In [daos/Jenkinsfile](daos/Jenkinsfile), replace the static clusterBoxStages cb_01/cb_02/cb_03 mapping with local helper logic that computes the effective `cb` test tag input using the same inputs the current stage 01 uses.
6. In [daos/Jenkinsfile](daos/Jenkinsfile), call getTestsInStage for that `cb` test tag input and capture the ordered list of matching test files.
7. Extend [pipeline-lib/vars/getTestsInStage.groovy](pipeline-lib/vars/getTestsInStage.groovy) with an optional timeout-enrichment mode that returns tuples/maps for each discovered test, e.g. `(test_file, test_name, timeout_value, timeout_source)`.
8. In timeout-enrichment mode, resolve timeout precedence as: method-specific entry from `timeouts:` (keyed by test method) first, fallback to top-level `timeout` in the YAML, else `null`/missing.
9. Add an optional parameter to [pipeline-lib/vars/getTestsInStage.groovy](pipeline-lib/vars/getTestsInStage.groovy) (for example `sort_by_timeout_desc: true`) that sorts discovered tests by `timeout_value` in descending order when enabled; keep default behavior as discovery order when not enabled.
10. Define stable tie-break behavior for equal or missing timeout values when `sort_by_timeout_desc` is enabled (recommended: preserve original discovery order for ties and place missing timeout values at the end).
11. In [daos/Jenkinsfile](daos/Jenkinsfile), add a local helper that splits that discovered list into 3 balanced partitions while preserving the selected ordering strategy.
12. In [pipeline-lib/vars/getFunctionalTestStage.groovy](pipeline-lib/vars/getFunctionalTestStage.groovy), add one optional override input such as explicit_test_tag or resolved_test_tag that accepts either a single test tag string or a list of test tags. If present, use it directly instead of calling getFunctionalTags.
13. Keep the rest of getFunctionalTestStage unchanged: node selection, checkout, functionalTest invocation, post-processing, and job_status updates should continue to work as they do now.
14. Back in [daos/Jenkinsfile](daos/Jenkinsfile), generate 3 getFunctionalTestStage calls dynamically for stages 01, 02, and 03, reusing the current names and pragma suffixes.
15. Pass each partition into getFunctionalTestStage through the new explicit test-tag override instead of stage_tags like medium,cb_01 or medium,cb_02.
16. Keep the existing cluster label, image version, nvme mode, run_if_pr, run_if_landing, and job_status wiring unchanged.
17. Add explicit handling for short or empty results. Recommended: create only non-empty stages if the `cb` source list has fewer than 3 tests.

**Relevant files**
- [daos/Jenkinsfile](daos/Jenkinsfile) — dynamic discovery, partitioning, and stage creation
- [daos/src/tests/ftest](daos/src/tests/ftest) — source-level conversion of `cb_01`/`cb_02`/`cb_03` tags to `cb`
- [pipeline-lib/vars/getTestsInStage.groovy](pipeline-lib/vars/getTestsInStage.groovy) — shared test discovery helper with timeout enrichment and optional sorting
- [pipeline-lib/vars/testsInStage.groovy](pipeline-lib/vars/testsInStage.groovy) — boolean wrapper over getTestsInStage for skip-gating
- [pipeline-lib/vars/getFunctionalTestStage.groovy](pipeline-lib/vars/getFunctionalTestStage.groovy) — optional explicit test-tag override (string or list)
- [pipeline-lib/vars/runTestFunctionalV2.groovy](pipeline-lib/vars/runTestFunctionalV2.groovy) — already forwards test_tag into TEST_TAG
- [daos/src/tests/ftest/scripts/main.sh](daos/src/tests/ftest/scripts/main.sh) — already splits TEST_TAG into positional launch.py arguments
- [daos/src/tests/ftest/util/launch_utils.py](daos/src/tests/ftest/util/launch_utils.py) — already accepts explicit test files as launch inputs
- [daos/src/tests/ftest/util/apricot/apricot/test.py](daos/src/tests/ftest/util/apricot/apricot/test.py) — timeout precedence behavior for method-specific `timeouts` vs top-level `timeout`

**Verification**
1. Verify DAOS ftest sources no longer use `cb_01`/`cb_02`/`cb_03` and consistently use `cb`.
2. Verify getTestsInStage returns the same ordered list that testsInStage would have discovered for the same test tag input.
3. Verify testsInStage preserves current skip behavior, including the conservative fallback on discovery failure.
4. Verify timeout-enrichment mode returns ordered tuples/maps with `test_file`, `test_name`, `timeout_value`, and `timeout_source`.
5. Verify timeout relation precedence: method-specific `timeouts` entry wins, otherwise fallback to top-level `timeout`.
6. Verify `sort_by_timeout_desc` default is disabled and preserves discovery ordering.
7. Verify enabling `sort_by_timeout_desc` sorts by timeout descending, keeps stable ordering for ties, and places missing timeout values at the end.
8. Log the discovered `cb` test count, each partition size, and a sample of timeout tuples before parallel(hwStages).
9. Verify each generated cluster-box stage prints a different explicit test list.
10. Verify functionalTest runs only those files in each partition.
11. Verify no-match and small-list cases behave intentionally.
12. Verify non-cluster functional stages are unchanged.

**Key decisions**
- Included: source conversion from `cb_01`/`cb_02`/`cb_03` to one `cb` test tag in DAOS ftest code.
- Included: shared getTestsInStage helper moved out of testsInStage with optional timeout enrichment.
- Included: optional `sort_by_timeout_desc` parameter in getTestsInStage to sort discovered tests by timeout descending.
- Included: dynamic 3-way split from one `cb` seed set in the Jenkinsfile.
- Included: explicit_test_tag override supports both string and list of test tags.
- Included: minimal getFunctionalTestStage extension so the Jenkinsfile can hand off explicit test files.
- Excluded: broader refactors of getFunctionalTags, skipFunctionalTestStage, or parseStageInfo beyond wiring testsInStage through getTestsInStage.
- Assumed: split by count, preserve selected ordering strategy, keep existing 01/02/03 stage names and suffixes.

If you want a strict Jenkinsfile-only implementation with zero library change, the tradeoff is that you would have to stop using getFunctionalTestStage for these 3 stages and inline their stage bodies directly. The current helper API is the only thing blocking a pure Jenkinsfile solution.

---

## Reference: Tests by CB Tag and Timeout

### CB_01 Tests (18 tests)

| Test Name | Timeout (seconds) | Source |
|-----------|-------------------|--------|
| test_rebuild_0to10 | 2000 | rebuild.yaml |
| test_rebuild_12to15 | 1500 | rebuild.yaml |
| test_rebuild_16 | 800* | rebuild.yaml |
| test_rebuild_17 | 800* | rebuild.yaml |
| test_rebuild_18 | 800* | rebuild.yaml |
| test_rebuild_19 | 800* | rebuild.yaml |
| test_rebuild_20 | 800* | rebuild.yaml |
| test_rebuild_21 | 800* | rebuild.yaml |
| test_rebuild_22 | 800* | rebuild.yaml |
| test_rebuild_23 | 800* | rebuild.yaml |
| test_rebuild_24 | 800* | rebuild.yaml |
| test_rebuild_25 | 1500 | rebuild.yaml |
| test_rebuild_26 | 800* | rebuild.yaml |
| test_rebuild_27 | 1500 | rebuild.yaml |
| test_rebuild_28 | 1500 | rebuild.yaml |
| test_rebuild_29 | 800* | rebuild.yaml |
| test_rebuild_30 | 800* | rebuild.yaml |
| test_daos_rebuild_interactive | 1185 | suite.yaml |

### CB_02 Tests (23 tests)

| Test Name | Timeout (seconds) | Source |
|-----------|-------------------|--------|
| test_daos_degraded_mode | 450 | suite.yaml |
| test_daos_pool | 360 | suite.yaml |
| test_daos_container | 700 | suite.yaml |
| test_daos_epoch | 125 | suite.yaml |
| test_daos_verify_consistency | 105 | suite.yaml |
| test_daos_io | 350 | suite.yaml |
| test_daos_ec_io | 510 | suite.yaml |
| test_daos_ec_obj | 750 | suite.yaml |
| test_daos_object_array | 105 | suite.yaml |
| test_daos_array | 106 | suite.yaml |
| test_daos_kv | 105 | suite.yaml |
| test_daos_capability | 104 | suite.yaml |
| test_daos_epoch_recovery | 104 | suite.yaml |
| test_daos_md_replication | 104 | suite.yaml |
| test_daos_drain_simple | 3720 | suite.yaml |
| test_daos_oid_allocator | 640 | suite.yaml |
| test_daos_checksum | 500 | suite.yaml |
| test_daos_aggregate_ec | 200 | suite.yaml |
| test_daos_degraded_ec | 1900 | suite.yaml |
| test_daos_dedup | 220 | suite.yaml |
| test_daos_upgrade | 300 | suite.yaml |
| test_daos_pipeline | 60 | suite.yaml |

### CB_03 Tests (7 tests)

| Test Name | Timeout (seconds) | Source |
|-----------|-------------------|--------|
| test_daos_server_helper_format | 60 | daos_server_helper.yaml |
| test_nvme_telemetry_metrics | 90 | dmg_telemetry_nvme.yaml |
| test_telemetry_list_nvme | 90 | dmg_telemetry_nvme.yaml |
| test_pool_destroy_with_io | 360 | pool_destroy_race.yaml |
| test_tiers | 30 | storage_tiers.yaml |
| test_create_and_query | 180 | create_query.yaml |
| test_osa_online_parallel_test | 1110 | online_parallel_test.yaml |

**Note:** Tests marked with `*` use the default timeout (800 seconds for rebuild.yaml tests) since they don't have explicit timeout entries defined.

**Summary:**
- **CB_01:** 18 tests (rebuild tests, mostly with extended timeouts)
- **CB_02:** 23 tests (daos_test suite tests, varied timeouts)
- **CB_03:** 7 tests (control, server, pool, and OSA tests)
