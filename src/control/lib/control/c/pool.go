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

#include "daos_control_util.h"
*/
import "C"
import (
	"unsafe"

	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

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
func daos_control_pool_create(
	handle C.uintptr_t,
	uid C.uid_t,
	gid C.gid_t,
	_ *C.char, // grp - unused, reserved for future use
	tgts *C.d_rank_list_t,
	scmSize C.daos_size_t,
	nvmeSize C.daos_size_t,
	prop *C.daos_prop_t,
	svc *C.d_rank_list_t,
	poolUUID *C.uuid_t,
) C.int {
	ctx, rc := getContext(handle)
	if rc != 0 {
		return rc
	}

	// Convert C types to Go types
	goTgts := rankListFromC(tgts)
	goProps := propsFromC(prop)

	// Build the pool create request
	req := &control.PoolCreateReq{
		User:      uidToUsername(uint32(uid)),
		UserGroup: gidToGroupname(uint32(gid)),
	}

	if len(goTgts) > 0 {
		req.Ranks = goTgts
	}

	// Set tier bytes for manual sizing
	req.TierBytes = []uint64{uint64(scmSize), uint64(nvmeSize)}

	req.Properties = goProps

	// Call the control API
	resp, err := control.PoolCreate(ctx.ctx(), ctx.client, req)
	if err != nil {
		ctx.log.Errorf("PoolCreate failed: %v", err)
		return C.int(errorToRC(err))
	}

	// Convert results back to C types
	respUUID, err := uuid.Parse(resp.UUID)
	if err != nil {
		return C.int(errorToRC(err))
	}
	copyUUIDToC(respUUID, poolUUID)

	// Copy service replicas
	if svc != nil && len(resp.SvcReps) > 0 {
		svcRanks := make([]ranklist.Rank, len(resp.SvcReps))
		for i, r := range resp.SvcReps {
			svcRanks[i] = ranklist.Rank(r)
		}
		copyRankListToC(svcRanks, svc)
	}

	return 0
}

//export daos_control_pool_destroy
func daos_control_pool_destroy(
	handle C.uintptr_t,
	poolUUID *C.uuid_t,
	_ *C.char, // grp - unused, reserved for future use
	force C.int,
) C.int {
	ctx, rc := getContext(handle)
	if rc != 0 {
		return rc
	}

	req := &control.PoolDestroyReq{
		ID:        poolIDFromC(poolUUID),
		Recursive: true,
		Force:     force != 0,
	}

	err := control.PoolDestroy(ctx.ctx(), ctx.client, req)
	return C.int(errorToRC(err))
}

//export daos_control_pool_evict
func daos_control_pool_evict(
	handle C.uintptr_t,
	poolUUID *C.uuid_t,
	_ *C.char, // grp - unused, reserved for future use
) C.int {
	ctx, rc := getContext(handle)
	if rc != 0 {
		return rc
	}

	req := &control.PoolEvictReq{
		ID: poolIDFromC(poolUUID),
	}

	err := control.PoolEvict(ctx.ctx(), ctx.client, req)
	return C.int(errorToRC(err))
}

//export daos_control_pool_exclude
func daos_control_pool_exclude(
	handle C.uintptr_t,
	poolUUID *C.uuid_t,
	_ *C.char, // grp - unused, reserved for future use
	rank C.d_rank_t,
	tgtIdx C.int,
) C.int {
	ctx, rc := getContext(handle)
	if rc != 0 {
		return rc
	}

	req := newPoolRanksReq(poolUUID, rank, tgtIdx)
	resp, err := control.PoolExclude(ctx.ctx(), ctx.client, req)
	if err != nil {
		return C.int(errorToRC(err))
	}

	return C.int(errorToRC(resp.Errors()))
}

//export daos_control_pool_reintegrate
func daos_control_pool_reintegrate(
	handle C.uintptr_t,
	poolUUID *C.uuid_t,
	_ *C.char, // grp - unused, reserved for future use
	rank C.d_rank_t,
	tgtIdx C.int,
) C.int {
	ctx, rc := getContext(handle)
	if rc != 0 {
		return rc
	}

	req := newPoolRanksReq(poolUUID, rank, tgtIdx)
	resp, err := control.PoolReintegrate(ctx.ctx(), ctx.client, req)
	if err != nil {
		return C.int(errorToRC(err))
	}

	return C.int(errorToRC(resp.Errors()))
}

//export daos_control_pool_drain
func daos_control_pool_drain(
	handle C.uintptr_t,
	poolUUID *C.uuid_t,
	_ *C.char, // grp - unused, reserved for future use
	rank C.d_rank_t,
	tgtIdx C.int,
) C.int {
	ctx, rc := getContext(handle)
	if rc != 0 {
		return rc
	}

	req := newPoolRanksReq(poolUUID, rank, tgtIdx)
	resp, err := control.PoolDrain(ctx.ctx(), ctx.client, req)
	if err != nil {
		return C.int(errorToRC(err))
	}

	return C.int(errorToRC(resp.Errors()))
}

//export daos_control_pool_extend
func daos_control_pool_extend(
	handle C.uintptr_t,
	poolUUID *C.uuid_t,
	_ *C.char, // grp - unused, reserved for future use
	ranks *C.d_rank_t,
	ranksNr C.int,
) C.int {
	ctx, rc := getContext(handle)
	if rc != 0 {
		return rc
	}

	// Convert C rank array to Go slice
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

	err := control.PoolExtend(ctx.ctx(), ctx.client, req)
	return C.int(errorToRC(err))
}

//export daos_control_pool_list
func daos_control_pool_list(
	handle C.uintptr_t,
	_ *C.char, // grp - unused, reserved for future use
	npools *C.daos_size_t,
	pools *C.daos_mgmt_pool_info_t,
) C.int {
	ctx, rc := getContext(handle)
	if rc != 0 {
		return rc
	}

	req := &control.ListPoolsReq{
		NoQuery: true, // Don't query each pool for details
	}

	resp, err := control.ListPools(ctx.ctx(), ctx.client, req)
	if err != nil {
		return C.int(errorToRC(err))
	}

	// Set the number of pools
	numPools := C.daos_size_t(len(resp.Pools))
	if npools == nil {
		return C.int(daos.InvalidInput)
	}

	npoolsIn := *npools
	*npools = numPools

	// If pools is nil, just return the count
	if pools == nil {
		return 0
	}

	// Check if the provided array is large enough
	if npoolsIn < numPools {
		return C.int(daos.BufTooSmall)
	}

	// Fill in the pool info structures. Zero-initialize the output array to
	// ensure pointer fields (mgpi_label, mgpi_svc) that are only conditionally
	// set are NULL rather than uninitialized.
	C.memset(unsafe.Pointer(pools), 0,
		C.size_t(numPools)*C.size_t(unsafe.Sizeof(*pools)))
	poolsSlice := unsafe.Slice(pools, numPools)
	for i, p := range resp.Pools {
		// Copy UUID
		copyUUIDToC(p.UUID, &poolsSlice[i].mgpi_uuid)

		// Copy label (C.CString allocates memory - caller must free)
		if p.Label != "" {
			poolsSlice[i].mgpi_label = C.CString(p.Label)
		}

		// Copy service leader
		poolsSlice[i].mgpi_ldr = C.d_rank_t(p.ServiceLeader)

		// Copy service replicas - allocate rank list if needed (like original parse_pool_info)
		if len(p.ServiceReplicas) > 0 {
			if poolsSlice[i].mgpi_svc == nil {
				poolsSlice[i].mgpi_svc = C.d_rank_list_alloc(C.uint32_t(len(p.ServiceReplicas)))
				if poolsSlice[i].mgpi_svc == nil {
					return C.int(daos.NoMemory)
				}
			}
			copyRankListToC(p.ServiceReplicas, poolsSlice[i].mgpi_svc)
		}
	}

	return 0
}

//export daos_control_pool_list_free
func daos_control_pool_list_free(
	pools *C.daos_mgmt_pool_info_t,
	npools C.daos_size_t,
) {
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

//export daos_control_pool_set_prop
func daos_control_pool_set_prop(
	handle C.uintptr_t,
	poolUUID *C.uuid_t,
	propName *C.char,
	propValue *C.char,
) C.int {
	ctx, rc := getContext(handle)
	if rc != 0 {
		return rc
	}

	goName := goString(propName)
	goValue := goString(propValue)

	// Look up property by name and set its value
	allProps := daos.PoolProperties()
	handler, ok := allProps[goName]
	if !ok {
		return C.int(daos.InvalidInput)
	}

	prop := handler.GetProperty(goName)
	if err := prop.SetValue(goValue); err != nil {
		return C.int(errorToRC(err))
	}

	req := &control.PoolSetPropReq{
		ID:         poolIDFromC(poolUUID),
		Properties: []*daos.PoolProperty{prop},
	}

	err := control.PoolSetProp(ctx.ctx(), ctx.client, req)
	return C.int(errorToRC(err))
}

//export daos_control_pool_get_prop
func daos_control_pool_get_prop(
	handle C.uintptr_t,
	label *C.char,
	poolUUID *C.uuid_t,
	propName *C.char,
	propValue **C.char,
) C.int {
	ctx, rc := getContext(handle)
	if rc != 0 {
		return rc
	}

	goName := goString(propName)

	// Look up property by name
	allProps := daos.PoolProperties()
	handler, ok := allProps[goName]
	if !ok {
		return C.int(daos.InvalidInput)
	}

	// Determine pool ID (label or UUID)
	poolID := goString(label)
	if poolID == "" {
		poolID = poolIDFromC(poolUUID)
	}

	req := &control.PoolGetPropReq{
		ID:         poolID,
		Properties: []*daos.PoolProperty{handler.GetProperty(goName)},
	}

	props, err := control.PoolGetProp(ctx.ctx(), ctx.client, req)
	if err != nil {
		return C.int(errorToRC(err))
	}

	if len(props) == 0 {
		return C.int(daos.Nonexistent)
	}

	// Return the value as a C string (caller must free)
	if propValue != nil {
		*propValue = C.CString(props[0].Value.String())
	}

	return 0
}

//export daos_control_pool_update_ace
func daos_control_pool_update_ace(
	handle C.uintptr_t,
	poolUUID *C.uuid_t,
	_ *C.char, // grp - unused, reserved for future use
	ace *C.char,
) C.int {
	ctx, rc := getContext(handle)
	if rc != 0 {
		return rc
	}

	req := &control.PoolUpdateACLReq{
		ID: poolIDFromC(poolUUID),
		ACL: &control.AccessControlList{
			Entries: []string{goString(ace)},
		},
	}

	_, err := control.PoolUpdateACL(ctx.ctx(), ctx.client, req)
	return C.int(errorToRC(err))
}

//export daos_control_pool_delete_ace
func daos_control_pool_delete_ace(
	handle C.uintptr_t,
	poolUUID *C.uuid_t,
	_ *C.char, // grp - unused, reserved for future use
	principal *C.char,
) C.int {
	ctx, rc := getContext(handle)
	if rc != 0 {
		return rc
	}

	req := &control.PoolDeleteACLReq{
		ID:        poolIDFromC(poolUUID),
		Principal: goString(principal),
	}

	_, err := control.PoolDeleteACL(ctx.ctx(), ctx.client, req)
	return C.int(errorToRC(err))
}

//export daos_control_pool_rebuild_stop
func daos_control_pool_rebuild_stop(
	handle C.uintptr_t,
	poolUUID *C.uuid_t,
	_ *C.char, // grp - unused, reserved for future use
	force C.int,
) C.int {
	ctx, rc := getContext(handle)
	if rc != 0 {
		return rc
	}

	req := &control.PoolRebuildManageReq{
		ID:     poolIDFromC(poolUUID),
		OpCode: control.PoolRebuildOpCodeStop,
		Force:  force != 0,
	}

	err := control.PoolRebuildManage(ctx.ctx(), ctx.client, req)
	return C.int(errorToRC(err))
}

//export daos_control_pool_rebuild_start
func daos_control_pool_rebuild_start(
	handle C.uintptr_t,
	poolUUID *C.uuid_t,
	_ *C.char, // grp - unused, reserved for future use
) C.int {
	ctx, rc := getContext(handle)
	if rc != 0 {
		return rc
	}

	req := &control.PoolRebuildManageReq{
		ID:     poolIDFromC(poolUUID),
		OpCode: control.PoolRebuildOpCodeStart,
	}

	err := control.PoolRebuildManage(ctx.ctx(), ctx.client, req)
	return C.int(errorToRC(err))
}
