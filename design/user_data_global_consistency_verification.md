# User Data Global Consistency Verification

Source: <https://daosio.atlassian.net/wiki/spaces/DC/pages/11485151260/User+Data+Global+Consistency+Verification>

# 1 Summary

- **Problem**: Verifying user data integrity and consistency across multiple targets after a disaster is a practical requirement for a distributed storage system with redundancy protection. Catastrophic Recovery (**CR**) Milestone II focuses on enabling such data global consistency verification.
- **Proposal**: Refactor the DTX resync and checksum scrub logic and interfaces, integrate them into the DAOS Global Consistency Checker (**CHK**), and extend CHK so that CHK engines can perform data global consistency verification for multiple redundancy groups (**RDG**) in parallel.
- **Impact**: The enhanced CHK will help users to detect orphan or uncertain DTX entries, checksum mismatches where applicable, and objects with data corruption or broken redundancy.

# 2 Background

In DAOS Catastrophic Recovery milestone I, we have built CHK framework that drives DAOS engines to globally scan DAOS system for user metadata consistency verification and recovery, including the following passes/phases:

```
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
...
```

Currently, CR scan is up to container level (pass = `CONT_CLEANUP`), only pool and container related metadata global consistency can be verified and recovered. It is not enough. What end users really care about is the data itself. We will extend CHK to cover such part.

## 2.1 DTX resync

DTX is the DAOS transaction model. It is some kind of enhanced two-phase-commit protocol that preserves transactional semantics for modifications across multiple DAOS targets. If some DAOS targets that hold some modifications but crash before related DTX entries are fully committed, then the visibility of data associated with those partial `prepared` or `committed` DTX entries is uncertain. So, DTX status synchronization is a prerequisite for subsequent data global consistency verification.

DAOS already supports DTX resync in normal mode. We can refactor relevant logic and interfaces, integrate them into the CHK framework. On the other hand, CHK may face to more complex DTX situations, and must be able to handle them properly.

- **Orphan DTX**

    In current implementation, committed DTX entries are retained on participants (targets) for a period of time for DTX resync and client resend handling. Once exceed some (time or count) threshold, DTX aggregation will be triggered on related target(s) to remove some old committed DTX entries locally. It is possible that the DTX entry on some target cannot be committed for very long time because of system busy or network congestion, while the committed DTX entries for the same transaction on other targets have already been removed by DTX aggregation. If the DTX leader is switched under such case, such non-committed DTX entry will become orphan with uncertain status (since it is difficult to distinguish such case from aborted transaction).

    Under CR mode, CHK engines will scan the entire system and gather more information from other orphan DTX participants, such as related data visibility, then it may be able to handle orphan DTX entries properly.

## 2.2 Checksum scrub (if applicable)

After DTX resync, the user data visibility is solidified. If checksum is enabled, we will further verify user data (local) integrity (or validity) via comparing the stored checksum and the re-calculated one based on user data. Only valid data can be used for subsequent data global consistency verification.

In theory, directly triggering existing DAOS checksum scrub under CR mode maybe the most simple solution. But the data fetched by checksum scrub ULT for recalculating checksum will not be reused by subsequent data verification, as to the same data needs to be double loaded. That is inefficient. So we prefer to verify checksum and related data global consistency via single-pass data loading.

## 2.3 Data global consistency verification

DAOS supports data redundancy (replicated or EC object) for high availability under kinds of failures. But as time going some hardware maybe failed, or some potential software bugs maybe triggered, as to the data in some redundancy group(s) may contain inconsistencies:

- **Lost some data piece**: according to (replicated or EC object) layout algorithm, some object shard, or dkey, or akey, or SV/EV record should exist on related target, but is not there in reality.
- **Corrupted data piece**: the SV/EV payload from different shards in the redundancy group does not match one another. For example, different record size/index, or different payload, and etc.

On the other hand, efficiency is another non-ignorable factor, especially for the large system with EB level storage. Data global consistency verification will to be driven on multiple targets in parallel. Try to avoid repeated data loading and transferring.

# 3 Goals and Non-Goals

## 3.1 Goals

- **Search and recover (if possible) orphan DTX**:

    CHK engines will scan all active targets, find out orphan DTX entries. For each one, if related redundancy is broken, then mark it as `CORRUPTED`; otherwise, commit or abort it according to related data global consistency.

- **Find out corrupted checksum (if applicable)**:

    For unmatched checksum, if redundancy is enough, then recover it via reconstructing data from other redundancy in the redundancy group.

- **Detect data global inconsistency**:

    CHK engine will be able to find out data global inconsistency and report the trouble component (object shard, dkey, akey, or SV/EV record).

## 3.2 Non-Goals

- **Locate bad shard**

    For the system with checksum disabled, if some object shard is inconsistent with other(s) that belong to the same redundancy group, then whether a replicated object or EC object, it is difficult to know which shard is bad, especially when the failed shards count is equal to (or exceeds) the redundancy. The logic may have to perform multiple cycles (calculating and) verification. That will be quite complex and time-consumed. It is risky to include that in release-3.0 scope.

- **Recover redundancy**

    We are not able to recover data redundancy until we can locate the bad shard. That will be done in subsequent release via object-based rebuild.

# 4 Design

## 4.1 Refactor DTX resync to be controllable for CHK engine usage

Allow external users (such as pool map update, container open, and the coming CR scan) to control DTX resync via unified DTX APIs.

### 4.1.1 dtx_scan_cb

New callback hook for DTX resync sponsor to handle events during DTX status synchronization. It is also the channel for DTX logic to interact with control plane under some uncertain case, such as DTX resync for CHK hits orphan DTX entry.

```
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

### 4.1.2 dtx_resync_start

Locally trigger DTX resync for the specified pool on current engine.

```
int dtx_resync_start(struct ds_pool *pool, uint32_t pm_ver, uint32_t flags, bool wait,
                    dtx_scan_cb cb, void *cb_data, ABT_thread *ult);
```

For each pool in CR scanning, when move to the pass `CHK__CHECK_SCAN_PHASE__CSP_DTX_RESYNC`, its PS leader will send CHK IV (pass =`DTX_RESYNC`) message to all engines on which some active pool shards reside. The CHK IV message handler will trigger DTX resync via such API on related engine&rsquo;s system XStream. And then `dtx_resync_start()` will create collective tasks on all local targets to synchronize DTX status in parallel at background, something as following:

```
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

### 4.1.3 dtx_resync_stop

Anytime, if the in-processing DTX resync needs to be stopped, such as when pool service shutdown (`ds_pool_stop`), then `dtx_resync_stop()` can be used.

```
void dtx_resync_stop(struct ds_pool *pool, bool wait);
```

It is the lower layer helper for `dmg check stop` after CHK engine scan moving to pass = `DTX_RESYNC`.

## 4.2 Checksum verification on sender

During user data global consistency verification with checksum enabled, server side workload may be high because all user data in the system (including all redundancy) will be loaded at least once. Workload distribution is therefore an important factor for CR efficiency.

For a normal client-sponsored fetch, the server returns data together with checksum , and the client verifies the checksum locally. That model is acceptable because clients usually have more available resources than storage servers.

For CHK usage, the situation is different. The fetch is sponsored by one server, while the data and checksums are returned by other servers. If all checksum verification is performed by the sponsor, the workload may become significantly unbalanced.

So we prefer to shift checksum verification workload from data receiver to the sender for CHK case. In other words, `ds_obj_rw_handler()` should verify the corresponding checksum before replying to fetch RPC. A new RPC flag `ORF_SERVER_VERIFY_CSUM` can be added to control the behavior.

```
diff --git a/src/object/cli_shard.c b/src/object/cli_shard.c
index c4a0de102c..02f7cd1b2a 100644
--- a/src/object/cli_shard.c
+++ b/src/object/cli_shard.c
@@ -1015,9 +1015,11 @@ dc_rw_cb(tse_task_t *task, void *arg)
                if (rc != 0)
                        goto out;

-               rc = rw_cb_csum_verify(rw_args);
-               if (rc != 0)
-                       goto out;
+               if (!(flags & ORF_SERVER_VERIFY_CSUM)) {
+                       rc = rw_cb_csum_verify(rw_args);
+                       if (rc != 0)
+                               goto out;
+               }

                if (rw_args->maps != NULL && orwo->orw_maps.ca_count > 0) {
                        daos_iom_t                      *reply_maps;
@@ -1112,6 +1114,9 @@ dc_obj_shard_rw(struct dc_obj_shard *shard, enum obj_rpc_opc opc,
        if (auxi->epoch.oe_flags & DTX_EPOCH_UNCERTAIN)
                flags |= ORF_EPOCH_UNCERTAIN;

+       if (for_chk)
+               flags |= ORF_SERVER_VERIFY_CSUM;
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
+               else if (flags & ORF_SERVER_VERIFY_CSUM)
+                       rc = cc_verify_orig_extents(ctx);
                else
                        rc = cc_copy(ctx);
```

This is not an entirely new concept. Current IO path may perform sender-side checksum verification for certain non-aligned partial fetch cases. In those cases, the checksum may be verified on both the sender and the receiver, which is redundant for CHK usage.

Another benefit of this change is that checksum no longer needs to be transferred over the network for CHK fetch requests, which can reduce network load.

**NOTE**: the risk of data failure during network transfer is ignored since server-to-server is trustable.

## 4.3 New driver for user data global verification

It is the core part for catastrophic recovery milestone II, some principles:

- **Completed scan**: Scan all objects, in spite of on which target, no omit.
- **Load balance**: It is expected that all CHK engines can share the verification workload.
- **Efficiency**: Try to avoid repeated data loading, transferring, reconstructing (if applicable).

### 4.3.1 Per redundancy group based scan leader

For each object redundancy group, select a leader who will sponsor object enumeration, data fetch (from all shards within the redundancy group) and verify the consistency. For different redundancy groups, try the best to select scan leaders on different targets. For example, calculating scan leader via hash object shard ID (and together with redundancy group index) against the redundancy group size. If there are enough objects, then most of engines will have chance to be as the scan leader for data verification, then the balance result will not be too bad.

```
leader_off = (oid.id_pub.lo + grp_idx) % grp_size;
```

**NOTE**: data size maybe quite different among objects, that also affects CHK workload, needs to be considered in the future.

### 4.3.2 Avoid missing object shards on non-leaders

There is an issue in above scan leader logic: if some object shards only exist on some non-leaders, but not on the scan leader (for EC object, it maybe normal), then how can the scan leader be aware of its leadership role on the target with related object shard absence. One possible solution is that calculating scan leader based on global unified OIT table snapshot.

When CHK logic moves to scanning pass =`OBJ_SCRUB`, for each container, the PS leader will firstly create an OIT table snapshot for the container. Then sends CHK IV (pass =`OBJ_SCRUB`) message to all engines on which some active (`UP` or `UPIN`) pool shards reside. The CHK IV message handler on related engines will generate (at least) one `obj_scrub` ULT for every container shard on every local target. Each `obj_scrub` ULT will iterate against its OIT table snapshot, for each object redundancy group, calculate the scan leader. If itself is the leader, then verifies the redundancy group; otherwise, handle next redundancy group or next object. It guarantees that all redundancy groups for all objects in the container can be handled by some scan leader without omitting.

#### 4.3.2.1 Unique scan leader for each redundancy group

For large container with a lot of objects, we may start multiple `obj_scrub` ULTs on each target. They will verify different redundancy groups concurrently. To avoid multiple `obj_scrub` ULTs to handle the same redundancy group, the scan leader selection algorithm can to be improved, for example:

```
target_off = (oid.id_pub.lo + grp_idx) % grp_size;
leader_off = target_off % ult_cnt;
```

### 4.3.3 Scan for inconsistency

The verification is per redundancy group based. Each redundancy group has each own scan leader.

#### 4.3.3.1 Verify replicated object

VOS aggregation may merge some EV records. That is per target based local asynchronous process, independent from one another. Then even if two replicas (in the same redundancy group) contain the same modifications series, their backend physical layouts maybe different. So data global verification for replicated object will be based on logical perspective as following:

1. For each replica, create a cursor to track verification progress.
2. Enumerate every (local and remote) replica at the same epoch.
3. Compare records in enumeration buffers: type, key, IOD geometry.
4. Advance cursor in parallel through each replica as comparison going.
5. Fetch data from each replica for SV/EV record, compare the payload.
6. Report inconsistency to control plane on any divergence.

**NOTE**: Each replica is loaded and transferred only once, no repeated verification.

**4.3.3.2 Verify EC object**

EC shards cannot be compared directly in the same way as replicated shards, because data shards and parity shards are not bytewise equivalent. For EC objects, consistency is defined as agreement between direct data access and the payload implied by the stripe&rsquo;s erasure-coding relationship. The verification flow is as follows:

<ol>
<li>Iterate through the shard positions in current EC redundancy group.</li>
<li>Enumerate records against current shard-specific view.</li>
<li>
  For SV/EV record on data shard:<br>
  a. Issue regular fetch request to current data shard.<br>
  b. Issue degraded fetch request that treats current shard as failed by force.<br>
  c. Compare the directly fetched data with the degraded reconstructed data.
</li>
<li>
  For SV/EV record on parity shard:<br>
  a. Fetch EC parity code (full update) or replica extent (partial update) from current parity shard.<br>
  b. Issue regular fetch request to related data shard(s) according to the SV/EV record index/offset.<br>
&nbsp;&nbsp;&nbsp;&nbsp;i. For case of EC replica extent, compare them as does for RP object replicas, otherwise<br>
&nbsp;&nbsp;&nbsp;&nbsp;ii. Re-calculate EC parity code based on the data from data shards, then compare them.
</li>
<li>Report inconsistency to control plane if unmatched.</li>
</ol>

**NOTE**: This approach validates whether each data shard is consistent with the rest of the redundancy group. It does not require direct equivalence between data shard and parity shard. Instead, it checks whether the content returned by a direct fetch matches the content implied by reconstruction from the other shard members. There maybe repeated data loading and transferring.

**4.3.3.3 Handle repeated EC inconsistency reports**

It is difficult to avoid repeated data verification in EC redundancy group, because:

- Different data view in one RDG, whether physical or logical.
- Complex full write, partial write, overwrite and aggregation cases.
- Limited resource and (expensive) overhead to track verification history.

Then, one inconsistency may be repeatedly detected during verifying different targets in the same redundancy group. For example: the corruption on data shard_0 for EC_4P2 object will be detected when verify shard_0, also can be found during subsequent verification for parity shard_{4,5} (and other data shard_{1,2,3} if it was generated via a full update).

If these repeated inconsistencies are directly reported to user/admin via control plane (`dmg check query`), they may seriously misguide user/admin as to make wrong decision or take wrong action in subsequent `dmg check repair`.

So the CHK scan leader needs to filter out repeated EC inconsistencies before reporting to control plane. For such purpose, each scan leader will maintain per redundancy group based tree to hold known EC inconsistencies. Such tree will be drained after verifying current redundancy group.

### 4.3.4 Backward reasoning for handle orphan DTX

The visibility for the data with orphan DTX is uncertain. That may affect the data global consistency. So if we assume some orphan DTX to be committed or aborted, then the visibility for related data will be changed accordingly as to may affect data global consistency result.

<ol>
<li>If some data global consistency is changed synchronously with related orphan DTX status assumption switching between committed or aborted, then we will know whether such orphan DTX should be committed or aborted.</li>
<li>
  If data keeps consistent whether assume related orphan DTX as committable or not, then related data attached with such orphan DTX was overwritten. Postpone to handle such orphan DTX until:<br>
  a. Hit some data matches case 1 on current target, or<br>
  b. Other participant for the same transaction has made decision on another target, or<br>
  c. The whole container is verified. If still undecided, then abort it.
</li>
<li>If data keeps inconsistent whether assume related orphan DTX as committable or not, then it is real data corruption case. Postpone to handle such orphan DTX as does for the case 2.</li>
</ol>

# 5 Implementation Phases

## 5.1 P1: Refactor DTX resync to be controllable for CHK engine usage

- CHK rank related interfaces, shared between CHK leader and CHK engine.

  - CHK leader: track the whole DTX process on all CHK engines.
  - CHK engine: track the process for DTX resync and data verification on pool shards.
- DTX resync interfaces: `dtx_scan_cb()`, `dtx_resync_start()` and `dtx_resync_stop()`.
- Integrate DTX resync into CHK engine and the callback `chk_engine_dtx_resync_cb()`.

## 5.2 P2: Checksum verification on sender

- IO handler verifies checksum for `DAOS_OBJ_RPC_FETCH` RPC with flags `ORF_SERVER_VERIFY_CSUM`.

## 5.3 P3: New driver for user data global verification

- CHK scan leader and OIT table snapshot.
- Replicated object verification.
- Filter for repeated inconsistency.
- EC object verification &ndash; for data shard.
- EC object verification &ndash; for parity shard.
- Handle orphan DTX during verification.

# 6 Compatibility &amp; On-disk Impact

## 6.1 On-disk format changes

DAOS supports to mark `ORPHAN` against specified DTX entry as early than release-2.0, and allows to set `CORRUPTED` flag against specified object/key since release-2.8. As for checksum, CHK usage will not introduce on-disk layout changes. So there will be no DTX/checksum/data corruption related VOS compatibility issues when downgrade to release-2.8.

## 6.2 RPC layout changes and interoperability

Introduce new RPC flag `ORF_SERVER_VERIFY_CSUM` for `DAOS_OBJ_RPC_FETCH` RPC. CHK engine is the unique user. Then it only affects server-to-server fetch. So if do not allow mixed-versions of servers to run in the same cluster, then in spite of talking with new client or old one, that will be fine.

But if allow mixed-versions of servers to work together, then two possible situations:

- PS leader is new (release-3.x) but some CHK engine is old (release-2.x).

    The old CHK engine will not respond the CHK IV (pass = `DTX_RESYNC`) message. If the PS leader is not aware of that, then CR process will be blocked there; otherwise, since new leader knows it is working together with old servers, it will complete the CR process after pass = `CONT_CLEANUP`. Anyway, we will not move to pass = `OBJ_SCRUB`, then related RPC changes will not take effect.

- PS leader is old (release-2.x) but some CHK engine is new (release-3.x).

    Old PS leader will complete CR process after pass = `CONT_CLEANUP`. That is fine.

# 7 External Interfaces

Current existing `dmg check` commands are used to control CR scan, no new interface or parameter.

# 8 Testing &amp; Validation

- **Unit Tests**:

  - CR scan with orphan DTX &ndash; partial committed.
  - CR scans with orphan DTX &ndash; partial aborted.
  - CR scans with checksum lost.
  - CR scans with corrupted checksum.
  - RP/EC object lost object shard.
  - RP/EC object lost dkey.
  - RP/EC object lost akey.
  - RP/EC object with bad SV/EV record.
  - RR/EC object with corrupted payload.
  - EC parity shard lost replica extent.
  - EC parity shard lost parity code.
  - EC object has bad replica extent.
  - EC object has bad parity code.
- **Pressure and Performance Tests**:

  - CR scans single container with 100M EC_16P3G1 objects + checksum enabled.
  - CR scans single container with 600M RP_3G1 objects + checksum enabled.
  - CR scans single container with 100M EC_16P3G1 objects + checksum disabled.
  - CR scans single container with 600M RP_3G1 objects + checksum disabled.
  - CR scans single container with 100M EC_16P3GX objects + checksum disabled.
  - CR scans 10 containers, each has 10M EC_16P3G1 objects + checksum disabled.
  - CR scans 100 containers, each has 1M EC_16P3G1 objects + checksum disabled.

# 9 Risks, Mitigations and Future Works

## 9.1 Risks and Mitigations

| **Risk** | **Mitigation** |
| --- | --- |
| EC data piece for full update will be repeatedly verified on all shards in the redundancy group, then the workload for data loading, transferring and checksum verification (where applicable) will be times enlarged, depends on redundancy group size. For wide redundancy group, such as EC_16P3, related overhead maybe terrible. | Scan leader can track verification history in DRAM, such as a partial enhanced VOS tree that is rooted from the object (shard). Any time when the payload for some record is verified (consistent or not), save it in the tree, only the record itself without payload. Before directly loading data for verification, check whether related extent has ever been (directly or indirectly) verified or not. Drain the verification history after verifying the redundancy group, remove part when not enough DRAM to hold all verification history. |

## 9.2 Future Works

### 9.2.1 Locate bad shard if detect inconsistency

When CHK engine detects data inconsistency in some redundancy group, if checksum is not enable, we may not have direct proof of which shard is faulty. Additional verification is required in that case.

#### 9.2.1.1 For replicated object

CHK engine will trust the majority. If more than half of the replicas are mutually consistent, the other minority replicas can be treated as faulty. Otherwise, such as a 2-way replicated object, or a 3-way replicated object where all copies differ, then have to interact with user/admin to make the decision.

#### 9.2.1.2 For EC object

For EC object, if hit inconsistency, then recursively disable current shard (temporarily) and continue verification among remaining shards in the redundancy group until get agreement for consistency among some shards or redundancy is broken. If got consistency agreement, then the last disabled shard is the bad one; otherwise, CHK engine cannot uniquely identify the faulty shard(s) and will defer to user/admin input.

**NOTE**: there may be multiple bad shards in the EC redundancy group, if found one, then re-enable other temporarily disabled ones, and repeat above process.

Consider an `EC_4P2` object as an example. Suppose CHK detects an inconsistency when comparing `shard_0` with the data reconstructed from `shard_1` through `shard_5`. CHK can then temporarily exclude `shard_0` and compare `shard_1` with data reconstructed from `shard_2` through `shard_5`. If that comparison succeeds, `shard_0` is likely to be the faulty shard. Otherwise, `shard_0` may be correct, and CHK can continue by testing `shard_2` against reconstruction from the remaining shards. By continuing this process, the faulty shard(s) can eventually be identified when the number of bad shards is below the redundancy limit.

### 9.2.2 Recover redundancy

After locating the bad shard(s), we will refactor some rebuild logic, then CHK engine can reuse them to recover the redundancy for specified object shard, dkey, akey or SV/EV record. That will be much efficient than current pool based rebuild.

### 9.2.3 Snapshot verification

By default, CHK engine will verify user data global consistent against the latest epoch. But it also can verify the consistency against snapshot if there is. Some difference: if found too much corruption as to difficult to recover, then may allow user/admin to destroy related snapshot via `dmg check repair`.

On the other hand, one snapshot means one cycle data loading and transferring, then it will be time-consumed. We will introduce new `dmg check start` option (`--snapshot`) to allow the user/admin to specify whether verify snapshot or not.

- `--snapshot=0`: do not verify snapshot (by default).
- `--snapshot=-1`: verify all snapshots.
- `--snapshot=epoch`: only verify the snapshot with the given `epoch`.
