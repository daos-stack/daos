//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build test_stubs
// +build test_stubs

package api

import (
	"unsafe"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/lib/daos"
)

/*
#include <daos_errno.h>
#include <daos_mgmt.h>
*/
import "C"

var (
	daos_init_RC C.int = 0
)

func daos_init() C.int {
	return daos_init_RC
}

func daos_fini() {}

func dc_agent_fini() {}

var (
	defaultSystemInfo *daos.SystemInfo = &daos.SystemInfo{
		Name:      build.DefaultSystemName,
		Provider:  "ofi+tcp",
		AgentPath: "/does/not/exist",
		RankURIs: []*daos.RankURI{
			{Rank: 0, URI: "/does/not/exist"},
			{Rank: 1, URI: "/does/not/exist"},
			{Rank: 2, URI: "/does/not/exist"},
		},
		AccessPointRankURIs: []*daos.RankURI{
			{Rank: 0, URI: "/does/not/exist"},
			{Rank: 1, URI: "/does/not/exist"},
			{Rank: 2, URI: "/does/not/exist"},
		},
	}
	daos_mgmt_get_sys_info_SystemInfo *daos.SystemInfo = defaultSystemInfo
	daos_mgmt_get_sys_info_RC         C.int            = 0
)

func daos_mgmt_get_sys_info(group *C.char, sys_info_out **C.struct_daos_sys_info) C.int {
	if daos_mgmt_get_sys_info_RC != 0 {
		return daos_mgmt_get_sys_info_RC
	}

	si := &C.struct_daos_sys_info{}
	for i, c := range daos_mgmt_get_sys_info_SystemInfo.Name {
		si.dsi_system_name[i] = C.char(c)
	}
	if group != nil && C.GoString(group) != daos_mgmt_get_sys_info_SystemInfo.Name {
		panic("invalid group")
	}
	for i, c := range daos_mgmt_get_sys_info_SystemInfo.Provider {
		si.dsi_fabric_provider[i] = C.char(c)
	}
	for i, c := range daos_mgmt_get_sys_info_SystemInfo.AgentPath {
		si.dsi_agent_path[i] = C.char(c)
	}

	si.dsi_nr_ranks = C.uint32_t(len(daos_mgmt_get_sys_info_SystemInfo.RankURIs))
	si.dsi_ranks = (*C.struct_daos_rank_uri)(C.calloc(C.size_t(si.dsi_nr_ranks), C.sizeof_struct_daos_rank_uri))
	if si.dsi_ranks == nil {
		panic("calloc() failed for system ranks")
	}
	rankSlice := unsafe.Slice(si.dsi_ranks, int(si.dsi_nr_ranks))
	for i, rankURI := range daos_mgmt_get_sys_info_SystemInfo.RankURIs {
		rankSlice[i].dru_rank = C.uint32_t(rankURI.Rank)
		rankSlice[i].dru_uri = C.CString(rankURI.URI)
	}

	si.dsi_nr_ms_ranks = C.uint32_t(len(daos_mgmt_get_sys_info_SystemInfo.AccessPointRankURIs))
	si.dsi_ms_ranks = (*C.uint32_t)(C.calloc(C.size_t(si.dsi_nr_ms_ranks), C.sizeof_uint32_t))
	if si.dsi_ms_ranks == nil {
		panic("calloc() failed for ms ranks")
	}
	msRankSlice := unsafe.Slice(si.dsi_ms_ranks, int(si.dsi_nr_ms_ranks))
	for i, rankURI := range daos_mgmt_get_sys_info_SystemInfo.AccessPointRankURIs {
		msRankSlice[i] = C.uint32_t(rankURI.Rank)
	}

	*sys_info_out = si
	return 0
}

func daos_mgmt_put_sys_info(sys_info *C.struct_daos_sys_info) {
	if sys_info == nil {
		return
	}

	if sys_info.dsi_ranks != nil {
		rankSlice := unsafe.Slice(sys_info.dsi_ranks, int(sys_info.dsi_nr_ranks))
		for _, rankURI := range rankSlice {
			C.free(unsafe.Pointer(rankURI.dru_uri))
		}
		C.free(unsafe.Pointer(sys_info.dsi_ranks))
	}

	if sys_info.dsi_ms_ranks != nil {
		C.free(unsafe.Pointer(sys_info.dsi_ms_ranks))
	}
}
