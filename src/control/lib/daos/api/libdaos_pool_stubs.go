//
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build test_stubs
// +build test_stubs

package api

import (
	"unsafe"

	"github.com/dustin/go-humanize"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

/*
#include <daos_errno.h>
#include <daos_mgmt.h>
#include <daos/pool.h>

#include "util.h"

static inline void
set_rebuild_state(struct daos_rebuild_status *drs, int32_t state)
{
	drs->rs_state = state;
}
*/
import "C"

func daos_gds2cds(sus []*daos.StorageUsageStats) C.struct_daos_space {
	return C.struct_daos_space{
		s_total: [2]C.uint64_t{
			C.uint64_t(sus[0].Total),
			C.uint64_t(sus[1].Total),
		},
		s_free: [2]C.uint64_t{
			C.uint64_t(sus[0].Free),
			C.uint64_t(sus[1].Free),
		},
	}
}

func daos_gpi2cpi(gpi *daos.PoolInfo) *C.daos_pool_info_t {
	cpi := &C.daos_pool_info_t{
		pi_uuid:      uuidToC(gpi.UUID),
		pi_ntargets:  C.uint32_t(gpi.TotalTargets),
		pi_nnodes:    C.uint32_t(gpi.TotalEngines),
		pi_ndisabled: C.uint32_t(gpi.DisabledTargets),
		pi_map_ver:   C.uint32_t(gpi.Version),
		pi_leader:    C.uint32_t(gpi.ServiceLeader),
		pi_bits:      C.uint64_t(gpi.QueryMask),
		pi_rebuild_st: C.struct_daos_rebuild_status{
			rs_errno:  C.int32_t(gpi.Rebuild.Status),
			rs_obj_nr: C.uint64_t(gpi.Rebuild.Objects),
			rs_rec_nr: C.uint64_t(gpi.Rebuild.Records),
		},
		pi_space: C.struct_daos_pool_space{
			ps_ntargets: C.uint32_t(gpi.ActiveTargets),
			ps_space:    daos_gds2cds(gpi.TierStats),
			ps_free_min: [2]C.uint64_t{
				C.uint64_t(gpi.TierStats[0].Min),
				C.uint64_t(gpi.TierStats[1].Min),
			},
			ps_free_max: [2]C.uint64_t{
				C.uint64_t(gpi.TierStats[0].Max),
				C.uint64_t(gpi.TierStats[1].Max),
			},
			ps_free_mean: [2]C.uint64_t{
				C.uint64_t(gpi.TierStats[0].Mean),
				C.uint64_t(gpi.TierStats[1].Mean),
			},
		},
	}

	// some funky mismatch between the Go/C states... fix this later.
	switch gpi.Rebuild.State {
	case daos.PoolRebuildStateIdle:
		cpi.pi_rebuild_st.rs_version = 0
	case daos.PoolRebuildStateBusy:
		cpi.pi_rebuild_st.rs_version = 1
		C.set_rebuild_state(&cpi.pi_rebuild_st, C.DRS_IN_PROGRESS)
	case daos.PoolRebuildStateDone:
		cpi.pi_rebuild_st.rs_version = 1
		C.set_rebuild_state(&cpi.pi_rebuild_st, C.DRS_COMPLETED)
	}
	return cpi
}

// defaultPoolInfo should be used to get a copy of the default pool info.
func defaultPoolInfo() *daos.PoolInfo {
	return copyPoolInfo(&daos_default_PoolInfo)
}

func copyPoolInfo(in *daos.PoolInfo) *daos.PoolInfo {
	if in == nil {
		return nil
	}

	out := new(daos.PoolInfo)
	*out = *in

	if in.Rebuild != nil {
		out.Rebuild = new(daos.PoolRebuildStatus)
		*out.Rebuild = *in.Rebuild
	}
	if in.TierStats != nil {
		out.TierStats = make([]*daos.StorageUsageStats, len(in.TierStats))
		for i, s := range in.TierStats {
			out.TierStats[i] = new(daos.StorageUsageStats)
			*out.TierStats[i] = *s
		}
	}
	if in.ServiceReplicas != nil {
		out.ServiceReplicas = make([]ranklist.Rank, len(in.ServiceReplicas))
		copy(out.ServiceReplicas, in.ServiceReplicas)
	}
	if in.EnabledRanks != nil {
		out.EnabledRanks = ranklist.NewRankSet()
		out.EnabledRanks.Replace(in.EnabledRanks)
	}
	if in.DisabledRanks != nil {
		out.DisabledRanks = ranklist.NewRankSet()
		out.DisabledRanks.Replace(in.DisabledRanks)
	}

	return out
}

var (
	daos_default_pool_connect_Handle C.daos_handle_t = C.daos_handle_t{cookie: 42}

	daos_default_PoolInfo daos.PoolInfo = daos.PoolInfo{
		QueryMask:       daos.DefaultPoolQueryMask,
		State:           daos.PoolServiceStateDegraded,
		UUID:            test.MockPoolUUID(1),
		Label:           "test-pool",
		TotalTargets:    48,
		TotalEngines:    3,
		ActiveTargets:   32,
		DisabledTargets: 16,
		Version:         2,
		ServiceLeader:   1,
		ServiceReplicas: []ranklist.Rank{0, 1, 2},
		EnabledRanks:    ranklist.MustCreateRankSet("0,2"),
		DisabledRanks:   ranklist.MustCreateRankSet("1"),
		Rebuild: &daos.PoolRebuildStatus{
			Status:  0,
			Objects: 1,
			Records: 2,
			State:   daos.PoolRebuildStateBusy,
		},
		TierStats: []*daos.StorageUsageStats{
			{
				MediaType: daos.StorageMediaTypeScm,
				Total:     64 * humanize.TByte,
				Free:      16 * humanize.TByte,
				Min:       1 * humanize.TByte,
				Max:       4 * humanize.TByte,
				Mean:      2 * humanize.TByte,
			},
			{
				MediaType: daos.StorageMediaTypeNvme,
				Total:     64 * humanize.PByte,
				Free:      16 * humanize.PByte,
				Min:       1 * humanize.PByte,
				Max:       4 * humanize.PByte,
				Mean:      2 * humanize.PByte,
			},
		},
	}

	daos_default_PoolHandle PoolHandle = PoolHandle{
		connHandle: connHandle{
			UUID:       daos_default_PoolInfo.UUID,
			Label:      daos_default_PoolInfo.Label,
			daosHandle: *defaultPoolHdl(),
		},
	}

	daos_default_PoolQueryTargetInfo daos.PoolQueryTargetInfo = daos.PoolQueryTargetInfo{
		Type:  daos.PoolQueryTargetType(1),
		State: daos.PoolTargetStateUp,
		Space: func() []*daos.StorageUsageStats {
			tiStats := make([]*daos.StorageUsageStats, len(daos_default_PoolInfo.TierStats))
			for i, tier := range daos_default_PoolInfo.TierStats {
				tiStats[i] = &daos.StorageUsageStats{
					MediaType: tier.MediaType,
					Total:     tier.Total,
					Free:      tier.Free,
				}
			}
			return tiStats
		}(),
	}
)

// defaultPoolHdl should be used to get a copy of the daos handle for the default pool.
func defaultPoolHdl() *C.daos_handle_t {
	newHdl := C.daos_handle_t{cookie: daos_default_pool_connect_Handle.cookie}
	return &newHdl
}

// defaultPoolHandle should be used to get a copy of the default pool handle.
func defaultPoolHandle() *PoolHandle {
	newHandle := new(PoolHandle)
	*newHandle = daos_default_PoolHandle
	return newHandle
}

// MockPoolHandle returns a valid PoolHandle suitable for use in tests.
func MockPoolHandle() *PoolHandle {
	return defaultPoolHandle()
}

func reset_daos_pool_stubs() {
	reset_daos_pool_connect()
	reset_daos_pool_disconnect()
	reset_daos_pool_query()
	reset_daos_pool_query_target()
	reset_daos_pool_list_attr()
	reset_daos_pool_get_attr()
	reset_daos_pool_set_attr()
	reset_daos_pool_del_attr()
	reset_daos_pool_list_cont()

	reset_daos_mgmt_list_pools()
}

var (
	daos_pool_connect_SetPoolID string
	daos_pool_connect_SetSys    string
	daos_pool_connect_SetFlags  daos.PoolConnectFlag
	daos_pool_connect_QueryMask daos.PoolQueryMask
	daos_pool_connect_Handle    *C.daos_handle_t = defaultPoolHdl()
	daos_pool_connect_Info      *daos.PoolInfo   = defaultPoolInfo()
	daos_pool_connect_Count     int              = 0
	daos_pool_connect_RCList    []C.int          = nil
	daos_pool_connect_RC        C.int            = 0
)

func reset_daos_pool_connect() {
	daos_pool_connect_SetPoolID = ""
	daos_pool_connect_SetSys = ""
	daos_pool_connect_SetFlags = 0
	daos_pool_connect_QueryMask = 0
	daos_pool_connect_Handle = defaultPoolHdl()
	daos_pool_connect_Info = defaultPoolInfo()
	daos_pool_connect_Count = 0
	daos_pool_connect_RCList = nil
	daos_pool_connect_RC = 0
}

func daos_pool_connect(poolID *C.char, sys *C.char, flags C.uint32_t, poolHdl *C.daos_handle_t, poolInfo *C.daos_pool_info_t, ev *C.struct_daos_event) C.int {
	daos_pool_connect_Count++
	if len(daos_pool_connect_RCList) > 0 {
		rc := daos_pool_connect_RCList[daos_pool_connect_Count-1]
		if rc != 0 {
			return rc
		}
	}
	if daos_pool_connect_RC != 0 {
		return daos_pool_connect_RC
	}

	// capture the parameters set by the test
	daos_pool_connect_SetPoolID = C.GoString(poolID)
	daos_pool_connect_SetSys = C.GoString(sys)
	daos_pool_connect_SetFlags = daos.PoolConnectFlag(flags)
	daos_pool_connect_QueryMask = daos.PoolQueryMask(poolInfo.pi_bits)

	// set the return values
	poolHdl.cookie = daos_pool_connect_Handle.cookie
	*poolInfo = *daos_gpi2cpi(daos_pool_connect_Info)

	return daos_pool_connect_RC
}

var (
	daos_pool_disconnect_Count int   = 0
	daos_pool_disconnect_RC    C.int = 0
)

func reset_daos_pool_disconnect() {
	daos_pool_disconnect_Count = 0
	daos_pool_disconnect_RC = 0
}

func daos_pool_disconnect(poolHdl C.daos_handle_t) C.int {
	daos_pool_disconnect_Count++
	return daos_pool_disconnect_RC
}

var (
	daos_pool_query_PoolInfo *daos.PoolInfo = defaultPoolInfo()
	daos_pool_query_RC       C.int          = 0
)

func reset_daos_pool_query() {
	daos_pool_query_PoolInfo = defaultPoolInfo()
	daos_pool_query_RC = 0
}

func daos_pool_query(poolHdl C.daos_handle_t, rankList **C.d_rank_list_t, retPoolInfo *C.daos_pool_info_t, props *C.daos_prop_t, ev *C.struct_daos_event) C.int {
	if daos_pool_query_RC != 0 {
		return daos_pool_query_RC
	}

	if retPoolInfo == nil {
		*rankList = ranklistFromGo(daos_pool_query_PoolInfo.DisabledRanks)
		return daos_pool_query_RC
	}

	queryBits := retPoolInfo.pi_bits
	*retPoolInfo = *daos_gpi2cpi(daos_pool_query_PoolInfo)
	retPoolInfo.pi_bits = queryBits

	if queryBits&C.DPI_ENGINES_ENABLED != 0 {
		*rankList = ranklistFromGo(daos_pool_query_PoolInfo.EnabledRanks)
	}
	if queryBits&C.DPI_ENGINES_DISABLED != 0 {
		*rankList = ranklistFromGo(daos_pool_query_PoolInfo.DisabledRanks)
	}

	if props != nil {
		propEntries := unsafe.Slice(props.dpp_entries, props.dpp_nr)
		for i := range propEntries {
			switch propEntries[i].dpe_type {
			case C.DAOS_PROP_PO_LABEL:
				C.set_dpe_str(&propEntries[i], C.CString(daos_pool_query_PoolInfo.Label))
			case C.DAOS_PROP_PO_SVC_LIST:
				rlPtr := ranklistFromGo(ranklist.RankSetFromRanks(daos_pool_query_PoolInfo.ServiceReplicas))
				C.set_dpe_val_ptr(&propEntries[i], (unsafe.Pointer)(rlPtr))
			}
		}
	}

	return daos_pool_query_RC
}

var (
	daos_pool_query_target_SetTgt  C.uint32_t                = C.uint32_t(ranklist.NilRank)
	daos_pool_query_target_SetRank C.uint32_t                = C.uint32_t(ranklist.NilRank)
	daos_pool_query_target_Info    *daos.PoolQueryTargetInfo = &daos_default_PoolQueryTargetInfo
	daos_pool_query_target_RC      C.int                     = 0
)

func reset_daos_pool_query_target() {
	daos_pool_query_target_SetTgt = C.uint32_t(ranklist.NilRank)
	daos_pool_query_target_SetRank = C.uint32_t(ranklist.NilRank)
	daos_pool_query_target_Info = &daos_default_PoolQueryTargetInfo
	daos_pool_query_target_RC = 0
}

func daos_pool_query_target(poolHdl C.daos_handle_t, tgt C.uint32_t, rank C.uint32_t, info *C.daos_target_info_t, ev *C.struct_daos_event) C.int {
	if daos_pool_query_target_RC != 0 {
		return daos_pool_query_target_RC
	}

	daos_pool_query_target_SetTgt = tgt
	daos_pool_query_target_SetRank = rank

	info.ta_type = C.daos_target_type_t(daos_pool_query_target_Info.Type)
	info.ta_state = C.daos_target_state_t(daos_pool_query_target_Info.State)
	info.ta_space = daos_gds2cds(daos_pool_query_target_Info.Space)

	return daos_pool_query_target_RC
}

var (
	daos_pool_list_attr_AttrList  daos.AttributeList = daos_default_AttrList
	daos_pool_list_attr_CallCount int
	daos_pool_list_attr_RCList    []C.int
	daos_pool_list_attr_RC        C.int = 0
)

func reset_daos_pool_list_attr() {
	daos_pool_list_attr_AttrList = daos_default_AttrList
	daos_pool_list_attr_CallCount = 0
	daos_pool_list_attr_RCList = nil
	daos_pool_list_attr_RC = 0
}

func daos_pool_list_attr(poolHdl C.daos_handle_t, buf *C.char, size *C.size_t, ev *C.struct_daos_event) C.int {
	return list_attrs(buf, size, daos_pool_list_attr_RCList, &daos_pool_list_attr_CallCount, daos_pool_list_attr_RC, daos_pool_list_attr_AttrList)
}

var (
	daos_pool_get_attr_SetN      int
	daos_pool_get_attr_ReqNames  map[string]struct{}
	daos_pool_get_attr_CallCount int
	daos_pool_get_attr_RCList    []C.int
	daos_pool_get_attr_AttrList  daos.AttributeList = daos_default_AttrList
	daos_pool_get_attr_RC        C.int              = 0
)

func reset_daos_pool_get_attr() {
	daos_pool_get_attr_SetN = 0
	daos_pool_get_attr_ReqNames = nil
	daos_pool_get_attr_CallCount = 0
	daos_pool_get_attr_RCList = nil
	daos_pool_get_attr_AttrList = daos_default_AttrList
	daos_pool_get_attr_RC = 0
}

func daos_pool_get_attr(poolHdl C.daos_handle_t, n C.int, names **C.char, values *unsafe.Pointer, sizes *C.size_t, ev *C.struct_daos_event) C.int {
	return get_attr(n, names, values, sizes, daos_pool_get_attr_RCList, &daos_pool_get_attr_CallCount, daos_pool_get_attr_RC, daos_pool_get_attr_AttrList, &daos_pool_get_attr_SetN, &daos_pool_get_attr_ReqNames)
}

var (
	daos_pool_set_attr_AttrList daos.AttributeList
	daos_pool_set_attr_RC       C.int = 0
)

func reset_daos_pool_set_attr() {
	daos_pool_set_attr_AttrList = nil
	daos_pool_set_attr_RC = 0
}

func daos_pool_set_attr(poolHdl C.daos_handle_t, n C.int, names **C.char, values *unsafe.Pointer, sizes *C.size_t, ev *C.struct_daos_event) C.int {
	return set_attr(n, names, values, sizes, daos_pool_set_attr_RC, &daos_pool_set_attr_AttrList)
}

var (
	daos_pool_del_attr_AttrNames []string
	daos_pool_del_attr_RC        C.int = 0
)

func reset_daos_pool_del_attr() {
	daos_pool_del_attr_AttrNames = nil
	daos_pool_del_attr_RC = 0
}

func daos_pool_del_attr(poolHdl C.daos_handle_t, n C.int, name **C.char, ev *C.struct_daos_event) C.int {
	return del_attr(n, name, daos_pool_del_attr_RC, &daos_pool_del_attr_AttrNames)
}

var (
	daos_pool_list_cont_RC C.int = 0
)

func reset_daos_pool_list_cont() {
	daos_pool_list_cont_RC = 0
}

func daos_pool_list_cont(poolHdl C.daos_handle_t, nCont *C.daos_size_t, conts *C.struct_daos_pool_cont_info, ev *C.struct_daos_event) C.int {
	if daos_pool_list_cont_RC != 0 {
		return daos_pool_list_cont_RC
	}

	return daos_pool_list_cont_RC
}

var (
	daos_mgmt_list_pools_SetSys    string
	daos_mgmt_list_pools_RetPools  []*daos.PoolInfo = []*daos.PoolInfo{defaultPoolInfo()}
	daos_mgmt_list_pools_CallCount int
	daos_mgmt_list_pools_RCList    []C.int
	daos_mgmt_list_pools_RC        C.int = 0
)

func reset_daos_mgmt_list_pools() {
	daos_mgmt_list_pools_SetSys = ""
	daos_mgmt_list_pools_RetPools = []*daos.PoolInfo{defaultPoolInfo()}
	daos_mgmt_list_pools_CallCount = 0
	daos_mgmt_list_pools_RCList = nil
	daos_mgmt_list_pools_RC = 0
}

func daos_mgmt_list_pools(sysName *C.char, poolCount *C.daos_size_t, pools *C.daos_mgmt_pool_info_t, ev *C.struct_daos_event) C.int {
	if len(daos_mgmt_list_pools_RCList) > 0 {
		rc := daos_mgmt_list_pools_RCList[daos_mgmt_list_pools_CallCount]
		daos_mgmt_list_pools_CallCount++
		if rc != 0 {
			return rc
		}
	}
	if daos_mgmt_list_pools_RC != 0 {
		return daos_mgmt_list_pools_RC
	}

	*poolCount = C.daos_size_t(len(daos_mgmt_list_pools_RetPools))

	daos_mgmt_list_pools_SetSys = C.GoString(sysName)
	if *poolCount == 0 || pools == nil {
		return daos_mgmt_list_pools_RC
	}

	poolSlice := unsafe.Slice(pools, *poolCount)
	for i, pool := range daos_mgmt_list_pools_RetPools {
		poolSlice[i].mgpi_uuid = uuidToC(pool.UUID)
		poolSlice[i].mgpi_label = C.CString(pool.Label)
		poolSlice[i].mgpi_svc = ranklistFromGo(ranklist.RankSetFromRanks(pool.ServiceReplicas))
		poolSlice[i].mgpi_ldr = C.d_rank_t(pool.ServiceLeader)
	}

	return daos_mgmt_list_pools_RC
}
