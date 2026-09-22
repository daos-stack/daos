# User Data Global Consistency Verification

Source: <https://daosio.atlassian.net/wiki/spaces/DC/pages/11485151260/User+Data+Global+Consistency+Verification>

## 1 Summary

- **Problem**: Verifying user data integrity and consistency across multiple targets after a disaster is a practical requirement for a distributed storage system with redundancy protection. Catastrophic Recovery (CR) Milestone II focuses on enabling such data global consistency verification.
- **Proposal**: Refactor the DTX resync and checksum scrub logic and interfaces, integrate them into the DAOS Global Consistency Checker (CHK), and extend CHK so that CHK engines can perform concurrent data global consistency verification in multiple redundancy groups.
- **Impact**: The enhanced CHK will help users to detect orphan or uncertain DTX entries, checksum mismatches where applicable, and objects with data corruption or broken redundancy.

## 2 Background

In DAOS Catastrophic Recovery milestone I, we have built CHK framework that drives DAOS engines to globally scan DAOS system for user metadata consistency verification and recovery, including the following passes/phases:

```c
typedef enum _Chk__CheckScanPhase {
  /*
   * Initial phase, prepare to start check on related engines.
   */
  CHK__CHECK_SCAN_PHASE__CSP_PREPARE = 0,
  /*
   * Pool list consolidation.
   */
  CHK__CHECK_SCAN_PHASE__CSP_POOL_LIST = 1,
  /*
   * Pool membership.
   */
  CHK__CHECK_SCAN_PHASE__CSP_POOL_MBS = 2,
  /*
   * Pool cleanup.
   */
  CHK__CHECK_SCAN_PHASE__CSP_POOL_CLEANUP = 3,
  /*
   * Container list consolidation.
   */
  CHK__CHECK_SCAN_PHASE__CSP_CONT_LIST = 4,
  /*
   * Container cleanup.
   */
  CHK__CHECK_SCAN_PHASE__CSP_CONT_CLEANUP = 5,
  /*
   * DTX resync and cleanup.
   */
  CHK__CHECK_SCAN_PHASE__CSP_DTX_RESYNC = 6,
  /*
   * RP/EC shards consistency verification with checksum scrub if have.
   */
  CHK__CHECK_SCAN_PHASE__CSP_OBJ_SCRUB = 7,
  /*
   * ...
   */
}
```

Currently, CR scan is up to container level (pass = `CONT_CLEANUP`), only pool and container related metadata global consistency can be verified and recovered. It is not enough. What end users really care about is the data itself. We will extend CHK to cover such part.

### 2.1 DTX resync

DTX is the DAOS transaction model. It is some kind of enhanced two-phase-commit protocol that preserves transactional semantics for modifications across multiple DAOS targets. If some DAOS targets that hold some modifications but crash before related DTX entries are fully committed, then the visibility of data associated with those partial `prepared` or `committed` DTX entries is uncertain. So, DTX status synchronization is a prerequisite for subsequent data global consistency verification.

DAOS already supports DTX resync in normal mode. We can refactor relevant logic and interfaces, integrate them into the CHK framework. On the other hand, CHK may face to more complex DTX situations, and must be able to handle them properly.

- **Orphan DTX**

In current implementation, committed DTX entries are retained on participants (targets) for a period of time for DTX resync and client resend handling. Once exceed some (time or count) threshold, DTX aggregation will be triggered on related target(s) to remove some old committed DTX entries locally. It is possible that the DTX entry on some target cannot be committed for very long time because of system busy or network congestion, while the committed DTX entries for the same transaction on other targets have already been removed by DTX aggregation. If the DTX leader is switched under such case, such non-committed DTX entry will become orphan with uncertain status (since it is difficult to distinguish such case from aborted transaction).

Under CR mode, because the entire system is scanned, CHK may be able to gather enough information from other participants to identify and possibly handle orphan DTX entries.

### 2.2 Checksum scrub (if applicable)

After DTX resync, the user data visibility is solidified. If checksum is enabled, we will further verify user data (local) integrity (or validity) via comparing the stored checksum and the re-calculated one based on user data. Only valid data can be used for subsequent data global consistency verification.

In theory, directly triggering existing DAOS checksum scrub under CR mode maybe the most simple solution. But the data fetched by checksum scrub ULT for recalculating checksum will not be reused by subsequent data verification, as to the same data needs to be double loaded. That is inefficient. So we prefer to verify checksum and related data global consistency via single-pass data loading.

### 2.3 Data global consistency verification

DAOS supports data redundancy (replicated or EC object) for high availability under kinds of failures. But as time going some hardware maybe failed, or some potential software bugs maybe triggered, as to the data in some redundancy group(s) may contain inconsistencies:

- **Lost some data piece**: according to (replicated or EC object) layout algorithm, some object shard, or dkey, or akey, or SV/EV record should exist on related target, but not there in reality.
- **Corrupted data piece**: the SV/EV payload from different shards in the redundancy group does not match one another. For example, different record size/index, or different content, and etc.

On the other hand, efficiency is another non-ignorable factor, especially for the large system with EB/ZB level storage. CR data global consistency verification needs to be driven on multiple targets concurrently. Try to avoid repeated data loading, transferring.

## 3 Goals and Non-Goals

### 3.1 Goals

- **Detect orphan DTX**: CHK engines will scan all active (`UP` or `UPIN`) targets, report orphan DTX entries on all active targets, in spite of it is DTX leader or not.
- **Checksum scrub**: For unmatched checksum, if related data redundancy can be recovered, then checksum will also be properly reset together with data recovery.
- **Data global verification**: CHK engine should has the ability to find out data global inconsistency and locate the trouble component (object shard, dkey, akey, or SV/EV record). If cannot recover, then mark corruption on all related redundancy in the group.

### 3.2 Non-Goals

- **Locate and recover bad shard**: For the system with checksum disabled, if the data in some object shard is verified as inconsistent with other(s) that belong to the same redundancy group, then in spite of it is a replicated object or EC object, it is not easy to decide which shard is bad, especially when the failed shards count is equal to (or exceeds) the redundancy. We may have to introduce multiple cycles (calculating and) verification. That will be quite complex and time-consumed, and not easy to be implemented within release-3.0 timeline. We prefer to handle that in subsequent release. Correspondingly, redundancy recovery will be done after we can locate the bad shard(s).

### 3.3 Optional

- **Recover orphan DTX**: According to the information stored inside orphan DTX entry, we can know which targets took part in such transaction, then via scanning related VOS tree on related targets. In theory, it is possible to know whether such DTX was ever (partial) committed (but removed by DTX aggregated) or aborted. Related recovery logic maybe time consumed, and needs to handle kinds of complex situations, such as related value is overwritten or merged by VOS aggregation.

## 4 Design

### 4.1 Refactor DTX resync to be controllable for CHK engine usage

Allow external users (such as pool map update, container open, and the coming CR scan) to control DTX resync via unified DTX APIs.

#### 4.1.1 dtx_scan_cb

New callback hook for DTX resync sponsor to handle events during DTX status synchronization. It is also the channel for DTX logic to interact with control plane under some uncertain case, such as DTX resync for CHK hits orphan DTX entry.

```c
typedef int (*dtx_scan_cb)(uint32_t event, int result, int tgt_id, uuid_t *cont_uuid,
                           struct dtx_id *xid, char *msg, void *data);

enum dtx_resync_event {
        DRE_DONE          = 1,
        DRE_FAIL          = 2,
        DRE_ENT_ORPHAN    = 3,
        DRE_ENT_CORRUPTED = 4,
};
```

For `DRE_ENT_ORPHAN` and `DRE_ENT_CORRUPTED` from DTX resync, CHK engine may further forward the interaction request to control plane (via CHK upcall).

#### 4.1.2 dtx_resync_start

Locally trigger DTX resync for the specified pool on current engine.

```c
int dtx_resync_start(struct ds_pool *pool, uint32_t pm_ver, uint32_t flags, bool wait,
                     dtx_scan_cb cb, void *cb_data, ABT_thread *ult);
```

For each pool in CR scanning, when move to the pass `CHK__CHECK_SCAN_PHASE__CSP_DTX_RESYNC`, its PS leader will send CHK IV (pass = `DTX_RESYNC`) message to all engines on which some active pool shards reside. The CHK IV message handler will trigger DTX resync via such API on related engine's system XStream. And then `dtx_resync_start()` will create collective tasks on all local targets to synchronize DTX status concurrently at background, something as following:

```text
PS leader:
chk_engine_pool_ult() => chk_engine_pool_notify(pass=CHK__CHECK_SCAN_PHASE__CSP_DTX_RESYNC)

CHK engine:
chk_engine_notify() => chk_pool_handle_notify() => chk_engine_dtx_resync() => dtx_resync_start()

static int
chk_engine_dtx_resync_cb(uint32_t event, int result, int tgt_id, uuid_t *cont_uuid,
                         struct dtx_id *xid, char *msg, void *data)

int
chk_engine_dtx_resync(struct chk_pool_rec *cpr)
{
        ...
        rc = dtx_resync_start(pool, pool->sp_map_version, RESYNC_FOR_CHK, false,
                              chk_engine_dtx_resync_cb, ult, &ult->ceu_ult);
        ...
        return rc;
}
```

#### 4.1.3 dtx_resync_stop

Anytime, if the in-processing DTX resync needs to be stopped, such as when pool service shutdown (`ds_pool_stop`), then `dtx_resync_stop()` can be used.

```c
void dtx_resync_stop(struct ds_pool *pool, bool wait);
```

It is the lower layer helper for `dmg check stop` after CHK engine scan moving to pass = `DTX_RESYNC`.

### 4.2 Checksum verification on sender

During user data global consistency verification with checksum enabled, server side workload may be high because all user data in the system (including all redundancy) will be loaded at least once. Workload distribution is therefore an important factor for CR efficiency.

For a normal client-sponsored fetch, the server returns data together with checksum, and the client verifies the checksum locally. That model is acceptable because clients usually have more available resources than storage servers.

For CHK usage, the situation is different. The fetch is sponsored by one server, while the data and checksums are returned by other servers. If all checksum verification is performed by the sponsor, the workload may become significantly unbalanced.

So we prefer to shift checksum verification workload from data receiver to the sender for CHK case. In other words, `ds_obj_rw_handler()` should verify the corresponding checksum before replying to fetch RPC. A new RPC flag `ORF_SERVER_VERIFY_CSUM` can be added to control the behavior.

```diff
diff --git a/src/object/cli_shard.c b/src/object/cli_shard.c
index c4a0de102c..02f7cd1b2a 100644
--- a/src/object/cli_shard.c
+++ b/src/object/cli_shard.c
@@ -1015,9 +1015,11 @@ dc_rw_cb(tse_task_t *task, void *arg)
 if (rc != 0)
         goto out;

-rc = rw_cb_csum_verify(rw_args);
-if (rc != 0)
-        goto out;
+if (!(flags & ORF_SERVER_VERIFY_CSUM)) {
+        rc = rw_cb_csum_verify(rw_args);
+        if (rc != 0)
+                goto out;
+}

 if (rw_args->maps != NULL && orwo->orw_maps.ca_count > 0) {
         daos_iom_t *reply_maps;
@@ -1112,6 +1114,9 @@ dc_obj_shard_rw(struct dc_obj_shard *shard, enum obj_rpc_opc opc,
 if (auxi->epoch.oe_flags & DTX_EPOCH_UNCERTAIN)
         flags |= ORF_EPOCH_UNCERTAIN;

+if (for_chk)
+        flags |= ORF_SERVER_VERIFY_CSUM;
+
 rc = dc_cont2uuid(shard->do_co, &cont_hdl_uuid, &cont_uuid);
 if (rc != 0)
         D_GOTO(out, rc);
diff --git a/src/object/obj_rpc.h b/src/object/obj_rpc.h
index 8e3db8291b..2787c800b0 100644
--- a/src/object/obj_rpc.h
+++ b/src/object/obj_rpc.h
@@ -193,6 +193,8 @@ enum obj_rpc_flags {
        ORF_CPD_RDONLY          = (1 << 25),
        /* Use for rebuild fetch epoch selection */
        ORF_FETCH_EPOCH_EC_AGG_BOUNDARY = (1 << 26),
+       /* Verify checksum on server for fetch. */
+       ORF_SERVER_VERIFY_CSUM  = (1 << 27),
 };
 /* clang-format on */
diff --git a/src/object/srv_csum.c b/src/object/srv_csum.c
index 0a32f14efd..2b3601fbe9 100644
--- a/src/object/srv_csum.c
+++ b/src/object/srv_csum.c
@@ -590,6 +590,8 @@ cc_add_csums_for_recx(struct csum_context *ctx, daos_recx_t *recx,
                 cc_skip_hole(ctx);
         else if (cc_need_new_csum(ctx, ctx->cc_cur_recx_idx))
                 rc = cc_create(ctx);
+        else if (flags & ORF_SERVER_VERIFY_CSUM)
+                rc = cc_verify_orig_extents(ctx);
         else
                 rc = cc_copy(ctx);
```

This is not an entirely new concept. Current IO path already performs sender-side checksum verification for certain non-aligned partial fetch cases. In those cases, the checksum may be verified on both the sender and the receiver, which is redundant for CHK usage.

An additional benefit of this design is that checksum no longer needs to be transferred over the network for CHK fetch requests, which can reduce network load.

**NOTE**: the risk of data failure during network transfer is ignored since server-to-server is trustable.

### 4.3 New driver for user data global verification

It is the core part for catastrophic recovery milestone II, some principles:

- **Efficiency**: Try to avoid repeated data loading and transferring, in spite of on which target.
- **Load balance**: It is expected that all CHK engines can share the verification workload.
- **Completed scan**: Scan all objects, in spite of on which target, no leak or skipped.

#### 4.3.1 Per redundancy group based scan leader

For each object redundancy group, select a leader who will sponsor object enumeration, data fetch (from all shards within the redundancy group) and verify the consistency. For different redundancy groups, try the best to select scan leaders on different targets. For example, calculating scan leader via hash object shard ID (and together with redundancy group index) against the redundancy group size. If there are enough objects, then most of engines will have chance to be as the scan leader for data verification, then the balance result will not be too bad.

```c
leader_off = (oid.id_pub.lo + grp_idx) % grp_size;
```

**NOTE**: data size maybe quite different among objects, that also affects CHK workload, needs to be considered in the future.

#### 4.3.2 Avoid missing object shards on non-leaders

There is an issue in above scan leader logic: if some object shards only exist on some non-leaders, but not on the scan leader (for EC object, such case maybe normal), then how can the scan leader avoid missing related object shards on non-leaders? CHK logic can resolve such issue via pushing object shards information from non-leaders to scan leader.

##### 4.3.2.1 Per target based `scan_for_leader` ULT

For a give pool, when CHK logic begins to data global consistency verification, its PS leader will send CHK IV (pass = `OBJ_SCRUB`) message to all engines on which some active (`UP` or `UPIN`) pool shards reside. The CHK IV message handler will generate `scan_for_leader` ULT on every local target. Each `scan_for_leader` ULT scans the object table in each container shard on the target:

- For the object shard which scan leader resides on current target locally, then insert its ID into the `chk_tobe_verify_tree` that is a pre target based two-level tree in DRAM. The firstly level index is `container_uuid`, the second level index is `object_shard_ID`.
- For the object shard which scan leader resides on other targets (in spite of on current engine or not), insert its ID into the `chk_remote_leader_tree` that is a pre target based three-level tree in DRAM. The firstly level index is `container_uuid`, the second level index is `target_ID`, the third level index is `object_shard_ID`.

```c
static void
chk_engine_scan_for_leader_ult(void *args)
{
        ...
        for_each_contaier(pool_shard) {
                for_each_obj(cont_shard) {
                        if (leader_on_local(obj_shard))
                                upsert_tree(chk_tobe_verify_tree, cont_uuid, oid);
                        else
                                insert_tree(chk_remote_leader_tree, cont_uuid, tgt_id, oid);
                }
        }
        ...
}
```

Consider potential DRAM pressure, only the first object shard ID (corresponding to the redundancy group) is stored in such two trees.

After `scan_for_leader` ULT completes the local scanning, it will send `CHK_OBJ_VERIFY` RPCs to other targets according to the information stored in the `chk_remote_leader_tree`. Those RPC push array of `cont_uuid+oid` to related remote scan leaders.

```diff
diff --git a/src/chk/chk_internal.h b/src/chk/chk_internal.h
index a6caa72653..70bfd24ce8 100644
--- a/src/chk/chk_internal.h
+++ b/src/chk/chk_internal.h
@@ -57,7 +57,9 @@
        X(CHK_REJOIN,                                                                           \
                0,      &CQF_chk_rejoin,        ds_chk_rejoin_hdlr,     NULL),                  \
        X(CHK_SET_POLICY,                                                                       \
-               0,      &CQF_chk_set_policy,    ds_chk_set_policy_hdlr, &chk_set_policy_co_ops)
+               0,      &CQF_chk_set_policy,    ds_chk_set_policy_hdlr, &chk_set_policy_co_ops) \
+       X(CHK_OBJ_VERIFY,                                                                       \
+               0,      &CQF_chk_obj_verify,    ds_chk_obj_verify_hdlr, NULL)
 /* clang-format on */

 /* Define for RPC enum population below */
@@ -293,6 +295,22 @@ CRT_RPC_DECLARE(chk_rejoin, DAOS_ISEQ_CHK_REJOIN, DAOS_OSEQ_CHK_REJOIN);
        ((uint32_t)             (cspo_padding)          CRT_VAR)

 CRT_RPC_DECLARE(chk_set_policy, DAOS_ISEQ_CHK_SET_POLICY, DAOS_OSEQ_CHK_SET_POLICY);
+
+/*
+ * CHK_OBJ_VERIFY:
+ * From check engine to scan leader to notify these objects should be verified by current scan leader.
+ */
+#define DAOS_ISEQ_CHK_OBJ_VERIFY                                               \
+       ((uint64_t)             (covi_gen)              CRT_VAR)                \
+       ((uuid_t)               (covi_pool_uuid)        CRT_VAR)                \
+       ((uuid_t)               (covi_cont_uuid)        CRT_VAR)                \
+       ((daos_unit_oid_t)      (covi_oids)             CRT_ARRAY)
+
+#define DAOS_OSEQ_CHK_OBJ_VERIFY                                               \
+       ((int32_t)              (covo_status)           CRT_VAR)                \
+       ((uint32_t)             (covo_padding)          CRT_VAR)
+
+CRT_RPC_DECLARE(chk_obj_verify, DAOS_ISEQ_CHK_OBJ_VERIFY, DAOS_OSEQ_CHK_OBJ_VERIFY);
 /* clang-format on */
```

The RPC handler `ds_chk_obj_verify_hdlr()` on remote target will add those `cont_uuid+oid` into its local `chk_tobe_verify_tree` on such remote target.

```c
static void
ds_chk_obj_verify_hdlr(crt_rpc_t *rpc)
{
        ...
        for_each_oid(rpc) {
                upsert_tree(chk_tobe_verify_tree, cont_uuid, oid);
        }
        ...
}
```

##### 4.3.2.2 Per target based `scan_for_verify` ULT

When CHK IV message handler (pass = `OBJ_SCRUB`) generates collective `scan_for_leader` ULT on all local targets, it also creates some `scan_for_verify` ULTs on all local target. Each object shard in the local `chk_tobe_verify_tree` stands for related redundancy group, and will be handled by some `scan_for_verify` ULT that enumerates all object shards in the redundancy group, fetches data and verifies whether consistent or not. When the redundancy group is verified, the OID will be moved to the `chk_verified_tree` that is also pre target based two-level tree in DRAM. The firstly level index is `container_uuid`, the second level index is `object_shard_ID`.

```c
static void
chk_engine_scan_for_verify_ult(void *args)
{
        ...
        while (!dbtree_is_empty(chk_tobe_verify_tree)) {
                for_each_item(chk_tobe_verify_tree) {
                        enumerate(obj_shard);
                        fetch_data(obj_shard);
                        compare(redundancy);
                }
                delete_tree(chk_tobe_verify_tree, cont_uuid, oid);
                insert_tree(chk_verified_tree, cont_uuid, oid);
        }
        ...
}
```

Anytime, before inserting oid (in spite of from local `scan_for_leader` or `ds_chk_obj_verify_hdlr`) into the `chk_tobe_verify_tree`, to avoid repeated verification, it needs to firstly check whether such oid exists in the `chk_verified_tree` or not.

#### 4.3.3 Scan for inconsistency

The verification is per redundancy group based. Each redundancy group has each own scan leader. Consider performance, we will start multiple `scan_for_verify` ULTs on each target, they will handle different OIDs in the shared `chk_tobe_verify_tree`.

##### 4.3.3.1 Verify replicated object

VOS aggregation may merge some EV records. That is per target based local asynchronous process, independent from one another. Then even if two replicas (in the same redundancy group) contain the same modifications series, their backend physical layouts maybe different. So data global verification for replicated object will be based on logical perspective as following:

1. Create one cursor per replica.
2. Enumerate every replica at the same epoch.
3. Check that existence state matches across replicas.
4. Advance all cursors in parallel through the key tree.
5. Compare record type, dkey, akey, extent geometry, record size, and payload.
6. Report inconsistency on any divergence.

##### 4.3.3.2 Verify EC object

EC shards cannot be compared directly in the same way as replicated shards, because data shards and parity shards are not bytewise equivalent. For EC objects, consistency is defined as agreement between direct data access and the payload implied by the stripe's erasure-coding relationship. The verification flow is as follows:

1. Iterate through the shard positions in the selected EC redundancy group.
2. Enumerate records from each shard-specific view.
3. For each data item, issue a normal fetch from current shard.
4. Issue a forced degraded fetch that treats current shard as failed.
5. Compare the directly fetched data with the degraded reconstructed data.
6. Report inconsistency if they are different.

This approach validates whether each data shard is consistent with the rest of the redundancy group. It does not require direct equivalence between data shards and parity shards. Instead, it checks whether the content returned by a direct fetch matches the content implied by reconstruction from the other shard members.

## 5 Implementation Phases

### 5.1 P1: Refactor DTX resync to be controllable for CHK engine usage

- DTX resync APIs: `dtx_resync_start()` and `dtx_resync_stop()`.
- CHK rank related interfaces, shared between CHK leader and CHK engine.
  - CHK leader: track the whole DTX process on all CHK engines.
  - CHK engine: track the process for DTX resync and data verification on pool shards.
- Integrate DTX resync into CHK engine and the callback `chk_engine_dtx_resync_cb()`.

### 5.2 P2: Checksum verification on sender

- IO handler verifies checksum for `DAOS_OBJ_RPC_FETCH` RPC with flags `ORF_SERVER_VERIFY_CSUM`.

### 5.3 P3: New driver for user data global verification

- Framework for `chk_tobe_verify_tree`, `chk_remote_leader_tree` and `chk_verified_tree`.
- `CHK_OBJ_VERIFY` RPC and its handler `ds_chk_obj_verify_hdlr()`.
- CHK engine enhancement, CHK IV (pass = `OBJ_SCRUB`) process.
- Scan leader election, `scan_for_leader` ULT.
- Inconsistency identification, `scan_for_verify` ULT.
  - Replicated object verification.
  - EC object verification.

## 6 Compatibility & On-disk Impact

### 6.1 On-disk format changes

DAOS supports to mark `ORPHAN` against specified DTX entry as early than release-2.0, and allows to set `CORRUPTED` flag against specified object/key since release-2.8. As for checksum, CHK usage will not introduce on-disk layout changes. So there will be no DTX/checksum/data corruption related VOS compatibility issues when downgrade to release-2.8.

### 6.2 RPC layout changes and interoperability

Introduce new RPC flag `ORF_SERVER_VERIFY_CSUM` for `DAOS_OBJ_RPC_FETCH` RPC. CHK engine is the unique user. Then it only affects server-to-server fetch. Another new added RPC is `CHK_OBJ_VERIFY` that is also server side only. So if do not allow mixed-versions of servers to run in the same cluster, then in spite of talking with new client or old one, that will be fine.

But if allow mixed-versions of servers to work together, then two possible situations:

- PS leader is new (release-3.x) but some CHK engine is old (release-2.x).

The old CHK engine will not respond the CHK IV (pass = `DTX_RESYNC`) message. If the PS leader is not aware of that, then CR process will be blocked there; otherwise, since new leader knows it is working together with old servers, it will complete the CR process after pass = `CONT_CLEANUP`. Anyway, we will not move to pass = `OBJ_SCRUB`, then related RPC changes will not take effect.

- PS leader is old (release-2.x) but some CHK engine is new (release-3.x).

Old PS leader will complete CR process after pass = `CONT_CLEANUP`. That is fine.

## 7 External Interfaces

Current existing `dmg check` commands are used to control CR scan, no new interface or parameter.

## 8 Testing & Validation

- Unit Tests:
  - CR scan with orphan DTX.
  - CR scan with checksum lost.
  - CR scan with corrupted checksum.
  - Double bad checksum in the same redundancy group.
  - Replicated/EC object lost object shard.
  - Replicated/EC object lost dkey.
  - Replicated/EC object lost akey.
  - Replicated/EC object with bad EV record (size, index, count).
  - Replicated/EC object with corrupted EV payload.
  - Replicated/EC object with corrupted SV payload.
  - Double failures for EC_4P2 object in the same redundancy group.
  - Lost some engine during CHK resynchronize DTX status.
  - Lost some engine during CHK verify data consistency.
- Pressure and Performance Tests:
  - CR for data global verification with 10M RP_3G1 objects.
  - CR for data global verification with 100M EC_16P2GX objects.

## 9 Risks, Mitigations and Future Works

### 9.1 Risks and Mitigations

| Risk | Mitigation |
| --- | --- |
| For a system with 100 engines and 8 targets per engine. If there are 100M `EC_4P2G1` objects in the system, then on each engine, total DRAM usage for the trees `chk_remote_leader_tree` and `chk_tobe_verify_tree` may exceed 3GB. That is huge, may cause OOM on server. | Send `CHK_OBJ_VERIFY` RPC before scan leader completes scanning, remove OID from `chk_remote_leader_tree` after RPC. |

### 9.2 Future Works

#### 9.2.1 Locate bad shard if detect inconsistency

When CHK engine detects data inconsistency in some redundancy group, if checksum is not enable, we may not have direct proof of which shard is faulty. Additional verification is required in that case.

##### 9.2.1.1 Failure count < Redundancy

- For replicated objects, CHK can trust the majority. If more than half of the replicas are mutually consistent, the minority replicas can be treated as faulty.
- For EC objects, consider an `EC_4P2` object as an example. Suppose CHK detects an inconsistency when comparing `shard_0` with the data reconstructed from `shard_1` through `shard_5`. CHK can then temporarily exclude `shard_0` and compare `shard_1` with data reconstructed from `shard_2` through `shard_5`. If that comparison succeeds, `shard_0` is likely to be the faulty shard. Otherwise, `shard_0` may be correct, and CHK can continue by testing `shard_2` against reconstruction from the remaining shards. By continuing this process, the faulty shard can eventually be identified when the number of bad shards is below the redundancy limit.

##### 9.2.1.2 Failure count >= Redundancy

- For replicated object, if no majority of mutually consistent replicas could be found, such as for a 2-way replicated object, or a 3-way replicated object where all copies differ, then have to interact with user/admin to make the choice.
- For EC objects, if inconsistencies remain regardless of which shard or shard set is excluded, then CHK engine cannot uniquely identify the faulty shard(s) and will defer to user/admin input.

At the beginning of the process, CHK does not know how many faulty shards exist in the redundancy group. The verification logic therefore begins with the optimistic assumption that the number of faulty shards is below the redundancy threshold and escalates only if that assumption fails.

#### 9.2.2 Recover redundancy

After locating the bad shard(s), we will refactor some rebuild logic, then CHK engine can reuse them to recover the redundancy for specified object shard, dkey, akey or SV/EV record. That will be much efficient than current pool based rebuild.

#### 9.2.3 Snapshot verification

By default, CHK engine will verify user data global consistent against the latest epoch. But it also can verify the consistency against snapshot if there is. Some difference: if found too much corruption as to difficult to recover, then may allow user/admin to destroy related snapshot via `dmg check repair`.

On the other hand, one snapshot means one cycle data loading and transferring, then it will be time-consumed. We will introduce new `dmg check start` option (`--snapshot`) to allow the user/admin to specify whether verify snapshot or not.

- `--snapshot=0`: do not verify snapshot (by default).
- `--snapshot=-1`: verify all snapshots.
- `--snapshot=epoch`: only verify the snapshot with the given `epoch`.