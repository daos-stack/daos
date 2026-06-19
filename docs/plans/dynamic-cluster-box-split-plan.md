## Plan: Dynamic Cluster Box Split

The split logic should move into [daos/Jenkinsfile](daos/Jenkinsfile), but one small pipeline-lib change is still needed. The reason is simple: today [pipeline-lib/vars/getFunctionalTestStage.groovy](pipeline-lib/vars/getFunctionalTestStage.groovy) can only rebuild its own tag expression from stage_tags/default_tags; it cannot accept a precomputed explicit list of test files, and that is what the Jenkinsfile needs to pass after it splits the cb_01 seed set.

**Steps**
1. In [daos/Jenkinsfile](daos/Jenkinsfile), replace the static clusterBoxStages cb_01/cb_02/cb_03 mapping with local helper logic that first computes the effective cb_01 selector using the same inputs the current stage 01 uses.
2. In [daos/Jenkinsfile](daos/Jenkinsfile), add a local helper that runs launch.py --list for that selector and captures the ordered list of matching test files.
3. In [daos/Jenkinsfile](daos/Jenkinsfile), add a local helper that splits that discovered list into 3 balanced partitions while preserving the original order.
4. In [pipeline-lib/vars/getFunctionalTestStage.groovy](pipeline-lib/vars/getFunctionalTestStage.groovy), add one optional override input such as explicit_test_tag or resolved_test_tag. If present, use it directly instead of calling getFunctionalTags.
5. Keep the rest of getFunctionalTestStage unchanged: node selection, checkout, functionalTest invocation, post-processing, and job_status updates should continue to work as they do now.
6. Back in [daos/Jenkinsfile](daos/Jenkinsfile), generate 3 getFunctionalTestStage calls dynamically for stages 01, 02, and 03, reusing the current names and pragma suffixes.
7. Pass each partition into getFunctionalTestStage through the new explicit selector override instead of stage_tags like medium,cb_01 or medium,cb_02.
8. Keep the existing cluster label, image version, nvme mode, run_if_pr, run_if_landing, and job_status wiring unchanged.
9. Add explicit handling for short or empty results. Recommended: create only non-empty stages if the cb_01 source list has fewer than 3 tests.

**Relevant files**
- [daos/Jenkinsfile](daos/Jenkinsfile) — dynamic discovery, partitioning, and stage creation
- [pipeline-lib/vars/getFunctionalTestStage.groovy](pipeline-lib/vars/getFunctionalTestStage.groovy) — optional explicit test-list override
- [pipeline-lib/vars/runTestFunctionalV2.groovy](pipeline-lib/vars/runTestFunctionalV2.groovy) — already forwards test_tag into TEST_TAG
- [daos/src/tests/ftest/scripts/main.sh](daos/src/tests/ftest/scripts/main.sh) — already splits TEST_TAG into positional launch.py arguments
- [daos/src/tests/ftest/util/launch_utils.py](daos/src/tests/ftest/util/launch_utils.py) — already accepts explicit test files as launch inputs

**Verification**
1. Log the discovered cb_01 test count and each partition size before parallel(hwStages).
2. Verify each generated cluster-box stage prints a different explicit test list.
3. Verify functionalTest runs only those files in each partition.
4. Verify no-match and small-list cases behave intentionally.
5. Verify non-cluster functional stages are unchanged.

**Key decisions**
- Included: dynamic 3-way split from one cb_01 seed set in the Jenkinsfile.
- Included: minimal getFunctionalTestStage extension so the Jenkinsfile can hand off explicit test files.
- Excluded: broader refactors of getFunctionalTags, skipFunctionalTestStage, or parseStageInfo.
- Assumed: split by count, preserve discovery order, keep existing 01/02/03 stage names and suffixes.

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
