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

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
)

/*
#include <daos_errno.h>
#include <daos/container.h>

#include "util.h"
*/
import "C"

func daos_gci2cci(gci *daos.ContainerInfo) *C.daos_cont_info_t {
	return &C.daos_cont_info_t{
		ci_uuid:       uuidToC(gci.ContainerUUID),
		ci_lsnapshot:  C.uint64_t(gci.LatestSnapshot),
		ci_nhandles:   C.uint32_t(gci.NumHandles),
		ci_nsnapshots: C.uint32_t(gci.NumSnapshots),
		ci_md_otime:   C.uint64_t(gci.OpenTime),
		ci_md_mtime:   C.uint64_t(gci.CloseModifyTime),
	}
}

// defaultContainerInfo should be used to get a copy of the default container info.
func defaultContainerInfo() *daos.ContainerInfo {
	return copyContainerInfo(&daos_default_ContainerInfo)
}

func copyContainerInfo(in *daos.ContainerInfo) *daos.ContainerInfo {
	if in == nil {
		return nil
	}

	out := new(daos.ContainerInfo)
	*out = *in

	return out
}

var (
	daos_default_cont_open_Handle C.daos_handle_t = C.daos_handle_t{cookie: 24}

	daos_default_ContainerInfo daos.ContainerInfo = daos.ContainerInfo{
		PoolUUID:        daos_default_PoolInfo.UUID,
		ContainerUUID:   test.MockPoolUUID(2),
		ContainerLabel:  "test-container",
		LatestSnapshot:  1,
		NumHandles:      2,
		NumSnapshots:    3,
		OpenTime:        4,
		CloseModifyTime: 5,
		Type:            daos.ContainerLayoutPOSIX,
		Health:          "HEALTHY",
		POSIXAttributes: &daos.POSIXAttributes{
			ChunkSize:       1024,
			ObjectClass:     1,
			DirObjectClass:  2,
			FileObjectClass: 3,
			Hints:           "what's a hint?",
		},
	}
)

// defaultContHandle should be used to get a copy of the default daos handle for a container.
func defaultContHdl() C.daos_handle_t {
	newHdl := C.daos_handle_t{cookie: daos_default_cont_open_Handle.cookie}
	return newHdl
}

// defaultContainerHandle should be used to get a copy of the default container handle.
func defaultContainerHandle() *ContainerHandle {
	return &ContainerHandle{
		connHandle: connHandle{
			UUID:       daos_default_ContainerInfo.ContainerUUID,
			Label:      daos_default_ContainerInfo.ContainerLabel,
			daosHandle: defaultContHdl(),
		},
		PoolHandle: defaultPoolHandle(),
	}
}

// MockContainerHandle returns a valid ContainerHandle suitable for use in tests.
func MockContainerHandle() *ContainerHandle {
	return defaultContainerHandle()
}

func reset_daos_cont_stubs() {
	reset_daos_cont_destroy()
	reset_daos_cont_open()
	reset_daos_cont_close()
	reset_daos_cont_query()
	reset_daos_cont_list_attr()
	reset_daos_cont_get_attr()
	reset_daos_cont_set_attr()
	reset_daos_cont_del_attr()
	reset_daos_cont_set_prop()
}

var (
	daos_cont_destroy_Count int
	daos_cont_destroy_RC    C.int = 0
)

func reset_daos_cont_destroy() {
	daos_cont_destroy_Count = 0
	daos_cont_destroy_RC = 0
}

func daos_cont_destroy(poolHdl C.daos_handle_t, contID *C.char, force C.int, ev *C.struct_daos_event) C.int {
	daos_cont_destroy_Count++
	return daos_cont_destroy_RC
}

var (
	daos_cont_open_SetContainerID string
	daos_cont_open_SetFlags       daos.ContainerOpenFlag
	daos_cont_open_Handle         = defaultContHdl()
	daos_cont_open_ContainerInfo  = defaultContainerInfo()
	daos_cont_open_Count          int
	daos_cont_open_RC             C.int = 0
)

func reset_daos_cont_open() {
	daos_cont_open_SetContainerID = ""
	daos_cont_open_SetFlags = 0
	daos_cont_open_Handle = defaultContHdl()
	daos_cont_open_ContainerInfo = defaultContainerInfo()
	daos_cont_open_Count = 0
	daos_cont_open_RC = 0
}

func daos_cont_open(poolHdl C.daos_handle_t, contID *C.char, flags C.uint, contHdl *C.daos_handle_t, contInfo *C.daos_cont_info_t, ev *C.struct_daos_event) C.int {
	daos_cont_open_Count++
	if daos_cont_open_RC != 0 {
		return daos_cont_open_RC
	}

	// capture the parameters set by the test
	daos_cont_open_SetContainerID = C.GoString(contID)
	daos_cont_open_SetFlags = daos.ContainerOpenFlag(flags)

	// set the return values
	contHdl.cookie = daos_cont_open_Handle.cookie
	if contInfo != nil {
		*contInfo = *daos_gci2cci(daos_cont_open_ContainerInfo)
	}

	return daos_cont_open_RC
}

var (
	daos_cont_close_Count int
	daos_cont_close_RC    C.int = 0
)

func reset_daos_cont_close() {
	daos_cont_close_Count = 0
	daos_cont_close_RC = 0
}

func daos_cont_close(contHdl C.daos_handle_t) C.int {
	daos_cont_close_Count++
	if daos_cont_close_RC != 0 {
		return daos_cont_close_RC
	}

	return daos_cont_close_RC
}

var (
	daos_cont_query_ContainerInfo       = defaultContainerInfo()
	daos_cont_query_RC            C.int = 0
)

func reset_daos_cont_query() {
	daos_cont_query_ContainerInfo = defaultContainerInfo()
	daos_cont_query_RC = 0
}

func daos_cont_query(contHdl C.daos_handle_t, contInfo *C.daos_cont_info_t, props *C.daos_prop_t, ev *C.struct_daos_event) C.int {
	if daos_cont_query_RC != 0 {
		return daos_cont_query_RC
	}

	if contInfo != nil {
		*contInfo = *daos_gci2cci(daos_cont_query_ContainerInfo)
	}

	if props != nil {
		propSlice := unsafe.Slice(props.dpp_entries, props.dpp_nr)
		for i := range propSlice {
			switch propSlice[i].dpe_type {
			case C.DAOS_PROP_CO_LABEL:
				cLabel := C.CString(daos_cont_query_ContainerInfo.ContainerLabel)
				C.set_dpe_str(&propSlice[i], cLabel)
			case C.DAOS_PROP_CO_REDUN_FAC:
				C.set_dpe_val(&propSlice[i], C.uint64_t(daos_cont_query_ContainerInfo.RedundancyFactor))
			case C.DAOS_PROP_CO_LAYOUT_TYPE:
				C.set_dpe_val(&propSlice[i], C.uint64_t(daos_cont_query_ContainerInfo.Type))
			case C.DAOS_PROP_CO_STATUS:
				if daos_cont_query_ContainerInfo.Health == "HEALTHY" {
					C.set_dpe_val(&propSlice[i], C.daos_prop_co_status_val(C.DAOS_PROP_CO_HEALTHY, 0, 0))
				} else {
					C.set_dpe_val(&propSlice[i], C.daos_prop_co_status_val(C.DAOS_PROP_CO_UNCLEAN, 0, 0))
				}
			}
		}
	}

	return daos_cont_query_RC
}

var (
	daos_cont_list_attr_AttrList  daos.AttributeList = daos_default_AttrList
	daos_cont_list_attr_CallCount int
	daos_cont_list_attr_RCList    []C.int
	daos_cont_list_attr_RC        C.int = 0
)

func reset_daos_cont_list_attr() {
	daos_cont_list_attr_AttrList = daos_default_AttrList
	daos_cont_list_attr_CallCount = 0
	daos_cont_list_attr_RCList = nil
	daos_cont_list_attr_RC = 0
}

func daos_cont_list_attr(contHdl C.daos_handle_t, buf *C.char, size *C.size_t, ev *C.struct_daos_event) C.int {
	return list_attrs(buf, size, daos_cont_list_attr_RCList, &daos_cont_list_attr_CallCount, daos_cont_list_attr_RC, daos_cont_list_attr_AttrList)
}

var (
	daos_cont_get_attr_SetN      int
	daos_cont_get_attr_ReqNames  map[string]struct{}
	daos_cont_get_attr_CallCount int
	daos_cont_get_attr_RCList    []C.int
	daos_cont_get_attr_AttrList  daos.AttributeList = daos_default_AttrList
	daos_cont_get_attr_RC        C.int              = 0
)

func reset_daos_cont_get_attr() {
	daos_cont_get_attr_SetN = 0
	daos_cont_get_attr_ReqNames = nil
	daos_cont_get_attr_CallCount = 0
	daos_cont_get_attr_RCList = nil
	daos_cont_get_attr_AttrList = daos_default_AttrList
	daos_cont_get_attr_RC = 0
}

func daos_cont_get_attr(contHdl C.daos_handle_t, n C.int, names **C.char, values *unsafe.Pointer, sizes *C.size_t, ev *C.struct_daos_event) C.int {
	return get_attr(n, names, values, sizes, daos_cont_get_attr_RCList, &daos_cont_get_attr_CallCount, daos_cont_get_attr_RC, daos_cont_get_attr_AttrList, &daos_cont_get_attr_SetN, &daos_cont_get_attr_ReqNames)
}

var (
	daos_cont_set_attr_AttrList daos.AttributeList
	daos_cont_set_attr_RC       C.int = 0
)

func reset_daos_cont_set_attr() {
	daos_cont_set_attr_AttrList = nil
	daos_cont_set_attr_RC = 0
}

func daos_cont_set_attr(contHdl C.daos_handle_t, n C.int, names **C.char, values *unsafe.Pointer, sizes *C.size_t, ev *C.struct_daos_event) C.int {
	return set_attr(n, names, values, sizes, daos_cont_set_attr_RC, &daos_cont_set_attr_AttrList)
}

var (
	daos_cont_del_attr_AttrNames []string
	daos_cont_del_attr_RC        C.int = 0
)

func reset_daos_cont_del_attr() {
	daos_cont_del_attr_AttrNames = nil
	daos_cont_del_attr_RC = 0
}

func daos_cont_del_attr(contHdl C.daos_handle_t, n C.int, name **C.char, ev *C.struct_daos_event) C.int {
	return del_attr(n, name, daos_cont_del_attr_RC, &daos_cont_del_attr_AttrNames)
}

var (
	daos_cont_set_prop_RC C.int = 0
)

func reset_daos_cont_set_prop() {
	daos_cont_set_prop_RC = 0
}

func daos_cont_set_prop(contHdl C.daos_handle_t, prop *C.daos_prop_t, ev *C.struct_daos_event) C.int {
	return daos_cont_set_prop_RC
}
