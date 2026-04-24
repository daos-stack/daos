//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

/*
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <uuid/uuid.h>
#include <daos_types.h>
#include <daos_prop.h>
#include <daos_mgmt.h>
#include <gurt/common.h>

#include <daos/control_types.h>

#include "daos_control_util.h"
*/
import "C"
import (
	"context"
	"fmt"
	"unsafe"

	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

// freePartialPoolList releases C memory in poolsSlice[0..n-1], matching
// daos_control_pool_list_free for the partial-population rollback path.
func freePartialPoolList(poolsSlice []C.daos_mgmt_pool_info_t, n int) {
	for i := 0; i < n; i++ {
		if poolsSlice[i].mgpi_label != nil {
			C.free(unsafe.Pointer(poolsSlice[i].mgpi_label))
			poolsSlice[i].mgpi_label = nil
		}
		if poolsSlice[i].mgpi_svc != nil {
			C.d_rank_list_free(poolsSlice[i].mgpi_svc)
			poolsSlice[i].mgpi_svc = nil
		}
	}
}

// validatePoolCreateArgs rejects malformed args before the RPC: nil args,
// nil pool_uuid (no output target), and the both-sizes-zero case. Errors
// wrap daos.InvalidInput so errorToRC collapses them to a single rc.
func validatePoolCreateArgs(args *C.struct_daos_control_pool_create_args) error {
	if args == nil {
		return fmt.Errorf("args is nil: %w", daos.InvalidInput)
	}
	if args.pool_uuid == nil {
		return fmt.Errorf("pool_uuid is nil: %w", daos.InvalidInput)
	}
	if args.scm_size == 0 && args.nvme_size == 0 {
		return fmt.Errorf("scm_size and nvme_size are both zero: %w", daos.InvalidInput)
	}
	return nil
}

// newPoolRanksReq creates a PoolRanksReq for single-rank operations
// that optionally include a target index.
func newPoolRanksReq(poolUUID *C.uuid_t, rank C.d_rank_t, tgtIdx C.int) *control.PoolRanksReq {
	req := &control.PoolRanksReq{
		ID:    poolIDFromC(poolUUID),
		Ranks: []ranklist.Rank{ranklist.Rank(rank)},
	}
	if tgtIdx >= 0 {
		req.TargetIdx = []uint32{uint32(tgtIdx)}
	}
	return req
}

//export daos_control_pool_create
func daos_control_pool_create(handle C.uintptr_t, args *C.struct_daos_control_pool_create_args) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
		if err := validatePoolCreateArgs(args); err != nil {
			ctx.log.Errorf("PoolCreate: %s", err)
			return err
		}

		goProps, err := propsFromC(args.prop)
		if err != nil {
			return err
		}

		userName, err := uidToUsername(uint32(args.uid))
		if err != nil {
			ctx.log.Errorf("PoolCreate: %s", err)
			return err
		}
		groupName, err := gidToGroupname(uint32(args.gid))
		if err != nil {
			ctx.log.Errorf("PoolCreate: %s", err)
			return err
		}

		req := &control.PoolCreateReq{
			User:      userName,
			UserGroup: groupName,
		}
		req.SetSystem(goString(args.grp))

		// svc.rl_nr serves as input capacity (a la dmg --nsvc) and is overwritten
		// with the output length by copyRankListToC. Capture the capacity before
		// the RPC so we can bound the replicas we write back.
		var svcCap int
		if args.svc != nil && args.svc.rl_nr > 0 {
			svcCap = int(args.svc.rl_nr)
			req.NumSvcReps = uint32(args.svc.rl_nr)
		}

		if goTgts := rankListFromC(args.tgts); len(goTgts) > 0 {
			req.Ranks = goTgts
		}

		req.TierBytes = []uint64{uint64(args.scm_size), uint64(args.nvme_size)}
		req.Properties = goProps

		resp, err := control.PoolCreate(ctx.ctx(), ctx.client, req)
		if err != nil {
			ctx.log.Errorf("PoolCreate failed: %v", err)
			return err
		}

		respUUID, err := uuid.Parse(resp.UUID)
		if err != nil {
			return err
		}
		copyUUIDToC(respUUID, args.pool_uuid)

		if args.svc != nil && len(resp.SvcReps) > 0 {
			svcRanks := make([]ranklist.Rank, len(resp.SvcReps))
			for i, r := range resp.SvcReps {
				svcRanks[i] = ranklist.Rank(r)
			}
			copyRankListToC(svcRanks, args.svc, svcCap)
		}

		return nil
	})
}

//export daos_control_pool_destroy
func daos_control_pool_destroy(handle C.uintptr_t, poolUUID *C.uuid_t, grp *C.char, force C.int) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
		req := &control.PoolDestroyReq{
			ID:        poolIDFromC(poolUUID),
			Recursive: true,
			Force:     force != 0,
		}
		req.SetSystem(goString(grp))
		return control.PoolDestroy(ctx.ctx(), ctx.client, req)
	})
}

//export daos_control_pool_evict
func daos_control_pool_evict(handle C.uintptr_t, poolUUID *C.uuid_t, grp *C.char) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
		req := &control.PoolEvictReq{ID: poolIDFromC(poolUUID)}
		req.SetSystem(goString(grp))
		return control.PoolEvict(ctx.ctx(), ctx.client, req)
	})
}

// poolRanksOp is the shared pool_exclude/reintegrate/drain body: fires a
// PoolRanks-style RPC and returns the first errored result (or nil).
func poolRanksOp(
	ctx *ctrlContext,
	poolUUID *C.uuid_t, grp *C.char, rank C.d_rank_t, tgtIdx C.int,
	rpc func(context.Context, control.UnaryInvoker, *control.PoolRanksReq) (*control.PoolRanksResp, error),
) error {
	req := newPoolRanksReq(poolUUID, rank, tgtIdx)
	req.SetSystem(goString(grp))
	resp, err := rpc(ctx.ctx(), ctx.client, req)
	if err != nil {
		return err
	}
	return firstRankStatus(resp.Results)
}

//export daos_control_pool_exclude
func daos_control_pool_exclude(handle C.uintptr_t, poolUUID *C.uuid_t, grp *C.char, rank C.d_rank_t, tgtIdx C.int) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
		return poolRanksOp(ctx, poolUUID, grp, rank, tgtIdx, control.PoolExclude)
	})
}

//export daos_control_pool_reintegrate
func daos_control_pool_reintegrate(handle C.uintptr_t, poolUUID *C.uuid_t, grp *C.char, rank C.d_rank_t, tgtIdx C.int) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
		return poolRanksOp(ctx, poolUUID, grp, rank, tgtIdx, control.PoolReintegrate)
	})
}

//export daos_control_pool_drain
func daos_control_pool_drain(handle C.uintptr_t, poolUUID *C.uuid_t, grp *C.char, rank C.d_rank_t, tgtIdx C.int) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
		return poolRanksOp(ctx, poolUUID, grp, rank, tgtIdx, control.PoolDrain)
	})
}

//export daos_control_pool_extend
func daos_control_pool_extend(handle C.uintptr_t, poolUUID *C.uuid_t, grp *C.char, ranks *C.d_rank_t, ranksNr C.int) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
		var goRanks []ranklist.Rank
		if ranks != nil && ranksNr > 0 {
			cRanks := unsafe.Slice(ranks, ranksNr)
			goRanks = make([]ranklist.Rank, ranksNr)
			for i, r := range cRanks {
				goRanks[i] = ranklist.Rank(r)
			}
		}

		req := &control.PoolExtendReq{
			ID:    poolIDFromC(poolUUID),
			Ranks: goRanks,
		}
		req.SetSystem(goString(grp))
		return control.PoolExtend(ctx.ctx(), ctx.client, req)
	})
}

// daos_control_pool_list lists pools managed by the system and fills in the
// caller-supplied array.
//
// Contract:
//   - On success (rc == 0), *npools is set to the number of entries written.
//     Each entry with a non-empty label has an mgpi_label allocated with
//     C.CString; each entry with service replicas has an mgpi_svc rank list
//     allocated with d_rank_list_alloc. The caller must release these by
//     passing the returned pools array and *npools to daos_control_pool_list_free.
//   - On error (rc != 0), *npools is set to 0 and no partial allocations
//     remain: the function frees any labels or rank lists it had already
//     allocated before returning. The caller does NOT need to call
//     daos_control_pool_list_free on error.
//   - If pools is nil, only *npools is set (to the required count) and rc==0
//     is returned; the caller can grow its buffer and call again.
//   - If *npools is less than the required count (and pools is non-nil),
//     BufTooSmall is returned and *npools is set to the required count.
//
//export daos_control_pool_list
func daos_control_pool_list(handle C.uintptr_t, grp *C.char, npools *C.daos_size_t, pools *C.daos_mgmt_pool_info_t) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
		if npools == nil {
			return daos.InvalidInput
		}

		req := &control.ListPoolsReq{NoQuery: true}
		req.SetSystem(goString(grp))

		resp, err := control.ListPools(ctx.ctx(), ctx.client, req)
		if err != nil {
			*npools = 0
			return err
		}

		numPools := C.daos_size_t(len(resp.Pools))

		// Count-only mode: caller passes nil pools to query the required size.
		if pools == nil {
			*npools = numPools
			return nil
		}

		// Return the required count either way so the caller can grow its
		// buffer and retry.
		npoolsIn := *npools
		*npools = numPools
		if npoolsIn < numPools {
			return daos.BufTooSmall
		}

		// Zero-init so conditionally-set pointer fields (mgpi_label, mgpi_svc) are NULL.
		C.memset(unsafe.Pointer(pools), 0,
			C.size_t(numPools)*C.size_t(unsafe.Sizeof(*pools)))
		poolsSlice := unsafe.Slice(pools, numPools)

		for i, p := range resp.Pools {
			copyUUIDToC(p.UUID, &poolsSlice[i].mgpi_uuid)

			// C.CString allocates; freed by daos_control_pool_list_free.
			if p.Label != "" {
				poolsSlice[i].mgpi_label = C.CString(p.Label)
			}

			poolsSlice[i].mgpi_ldr = C.d_rank_t(p.ServiceLeader)

			// On alloc failure, free prior entries and reset *npools = 0 so the
			// caller need not call pool_list_free.
			if len(p.ServiceReplicas) > 0 {
				poolsSlice[i].mgpi_svc = C.d_rank_list_alloc(C.uint32_t(len(p.ServiceReplicas)))
				if poolsSlice[i].mgpi_svc == nil {
					freePartialPoolList(poolsSlice, i+1)
					*npools = 0
					return daos.NoMemory
				}
				copyRankListToC(p.ServiceReplicas, poolsSlice[i].mgpi_svc, len(p.ServiceReplicas))
			}
		}

		*npools = numPools
		return nil
	})
}

//export daos_control_pool_list_free
func daos_control_pool_list_free(
	pools *C.daos_mgmt_pool_info_t,
	npools C.daos_size_t,
) {
	defer recoverExportVoid()

	if pools == nil || npools == 0 {
		return
	}

	poolsSlice := unsafe.Slice(pools, npools)
	for i := range poolsSlice {
		if poolsSlice[i].mgpi_label != nil {
			C.free(unsafe.Pointer(poolsSlice[i].mgpi_label))
			poolsSlice[i].mgpi_label = nil
		}
		if poolsSlice[i].mgpi_svc != nil {
			C.d_rank_list_free(poolsSlice[i].mgpi_svc)
			poolsSlice[i].mgpi_svc = nil
		}
	}
}

// resolvePoolID returns label if non-empty, otherwise the UUID string.
// label takes precedence to mirror dmg's handling of either identifier.
func resolvePoolID(label *C.char, poolUUID *C.uuid_t) string {
	if id := goString(label); id != "" {
		return id
	}
	return poolIDFromC(poolUUID)
}

//export daos_control_pool_set_prop
func daos_control_pool_set_prop(handle C.uintptr_t, label *C.char, poolUUID *C.uuid_t, propName, propValue *C.char) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
		handler, ok := daos.PoolProperties()[goString(propName)]
		if !ok {
			return daos.InvalidInput
		}
		prop := handler.GetProperty(goString(propName))
		if err := prop.SetValue(goString(propValue)); err != nil {
			return err
		}
		req := &control.PoolSetPropReq{
			ID:         resolvePoolID(label, poolUUID),
			Properties: []*daos.PoolProperty{prop},
		}
		return control.PoolSetProp(ctx.ctx(), ctx.client, req)
	})
}

//export daos_control_pool_get_prop
func daos_control_pool_get_prop(handle C.uintptr_t, label *C.char, poolUUID *C.uuid_t, propName *C.char, propValue **C.char) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
		goName := goString(propName)
		handler, ok := daos.PoolProperties()[goName]
		if !ok {
			return daos.InvalidInput
		}
		req := &control.PoolGetPropReq{
			ID:         resolvePoolID(label, poolUUID),
			Properties: []*daos.PoolProperty{handler.GetProperty(goName)},
		}
		props, err := control.PoolGetProp(ctx.ctx(), ctx.client, req)
		if err != nil {
			return err
		}
		if len(props) == 0 || !props[0].Value.IsSet() {
			return daos.Nonexistent
		}
		// C.CString allocates; caller must free. StringValue() renders enum
		// values by name (e.g. "timed" instead of "2" for scrub).
		if propValue != nil {
			*propValue = C.CString(props[0].StringValue())
		}
		return nil
	})
}

//export daos_control_pool_update_ace
func daos_control_pool_update_ace(handle C.uintptr_t, poolUUID *C.uuid_t, grp, ace *C.char) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
		req := &control.PoolUpdateACLReq{
			ID:  poolIDFromC(poolUUID),
			ACL: &control.AccessControlList{Entries: []string{goString(ace)}},
		}
		req.SetSystem(goString(grp))
		_, err := control.PoolUpdateACL(ctx.ctx(), ctx.client, req)
		return err
	})
}

//export daos_control_pool_delete_ace
func daos_control_pool_delete_ace(handle C.uintptr_t, poolUUID *C.uuid_t, grp, principal *C.char) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
		req := &control.PoolDeleteACLReq{
			ID:        poolIDFromC(poolUUID),
			Principal: goString(principal),
		}
		req.SetSystem(goString(grp))
		_, err := control.PoolDeleteACL(ctx.ctx(), ctx.client, req)
		return err
	})
}

//export daos_control_pool_rebuild_stop
func daos_control_pool_rebuild_stop(handle C.uintptr_t, poolUUID *C.uuid_t, grp *C.char, force C.int) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
		req := &control.PoolRebuildManageReq{
			ID:     poolIDFromC(poolUUID),
			OpCode: control.PoolRebuildOpCodeStop,
			Force:  force != 0,
		}
		req.SetSystem(goString(grp))
		return control.PoolRebuildManage(ctx.ctx(), ctx.client, req)
	})
}

//export daos_control_pool_rebuild_start
func daos_control_pool_rebuild_start(handle C.uintptr_t, poolUUID *C.uuid_t, grp *C.char) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
		req := &control.PoolRebuildManageReq{
			ID:     poolIDFromC(poolUUID),
			OpCode: control.PoolRebuildOpCodeStart,
		}
		req.SetSystem(goString(grp))
		return control.PoolRebuildManage(ctx.ctx(), ctx.client, req)
	})
}
