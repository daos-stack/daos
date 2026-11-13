//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

//go:build test_stubs

// cgo drivers for check_test.go (see test_helpers.go for why these can't
// live in the test file).

package main

/*
#include <stdlib.h>
#include <stdint.h>
#include <uuid/uuid.h>

#include <daos/control_types.h>
*/
import "C"
import (
	"runtime/cgo"
	"unsafe"

	"github.com/google/uuid"
)

func uuidArrayToC(uuids []uuid.UUID) []C.uuid_t {
	if len(uuids) == 0 {
		return nil
	}
	out := make([]C.uuid_t, len(uuids))
	for i, u := range uuids {
		copyUUIDToC(u, &out[i])
	}
	return out
}

func callCheckSwitch(handle cgo.Handle, enable bool) int {
	var e C.int
	if enable {
		e = 1
	}
	return int(daos_control_check_switch(C.uintptr_t(handle), e))
}

func callCheckStart(handle cgo.Handle, flags uint32, poolUUIDs []uuid.UUID, policies string) int {
	arr := uuidArrayToC(poolUUIDs)
	var uuids *C.uuid_t
	if len(arr) > 0 {
		uuids = &arr[0]
	}
	cPol, f := cString(policies)
	defer f()
	return int(daos_control_check_start(C.uintptr_t(handle), C.uint32_t(flags), C.uint32_t(len(poolUUIDs)), uuids, cPol))
}

func callCheckStop(handle cgo.Handle, poolUUIDs []uuid.UUID) int {
	arr := uuidArrayToC(poolUUIDs)
	var uuids *C.uuid_t
	if len(arr) > 0 {
		uuids = &arr[0]
	}
	return int(daos_control_check_stop(C.uintptr_t(handle), C.uint32_t(len(poolUUIDs)), uuids))
}

func callCheckQuery(handle cgo.Handle, poolUUIDs []uuid.UUID) int {
	arr := uuidArrayToC(poolUUIDs)
	var uuids *C.uuid_t
	if len(arr) > 0 {
		uuids = &arr[0]
	}
	return int(daos_control_check_query(C.uintptr_t(handle), C.uint32_t(len(poolUUIDs)), uuids, nil))
}

func callCheckRepair(handle cgo.Handle, seq uint64, action uint32) int {
	return int(daos_control_check_repair(C.uintptr_t(handle), C.uint64_t(seq), C.uint32_t(action)))
}

func callCheckSetPolicy(handle cgo.Handle, flags uint32, policies string) int {
	cPol, f := cString(policies)
	defer f()
	return int(daos_control_check_set_policy(C.uintptr_t(handle), C.uint32_t(flags), cPol))
}

type testCheckPoolInfo struct {
	UUID   uuid.UUID
	Status string
	Phase  string
}

type testCheckReportInfo struct {
	UUID    uuid.UUID
	Seq     uint64
	Class   uint32
	Action  uint32
	Result  int
	Options []int
}

type testCheckInfo struct {
	Status         string
	Phase          string
	Pools          []testCheckPoolInfo
	Reports        []testCheckReportInfo
	PostFreeZeroed bool
}

// callCheckQueryWithInfo populates a daos_check_info, snapshots it into Go
// values, frees the C allocation, and asserts (via PostFreeZeroed) that the
// free zeroed out the struct's pointers and counters.
func callCheckQueryWithInfo(handle cgo.Handle, poolUUIDs []uuid.UUID) (testCheckInfo, int) {
	arr := uuidArrayToC(poolUUIDs)
	var uuids *C.uuid_t
	if len(arr) > 0 {
		uuids = &arr[0]
	}

	var dci C.struct_daos_check_info
	rc := int(daos_control_check_query(C.uintptr_t(handle), C.uint32_t(len(poolUUIDs)), uuids, &dci))

	info := testCheckInfo{
		Status: goCString(dci.dci_status),
		Phase:  goCString(dci.dci_phase),
	}

	if dci.dci_pools != nil && dci.dci_pool_nr > 0 {
		pools := unsafe.Slice(dci.dci_pools, int(dci.dci_pool_nr))
		info.Pools = make([]testCheckPoolInfo, len(pools))
		for i := range pools {
			info.Pools[i] = testCheckPoolInfo{
				UUID:   uuidFromC(&pools[i].dcpi_uuid),
				Status: goCString(pools[i].dcpi_status),
				Phase:  goCString(pools[i].dcpi_phase),
			}
		}
	}
	if dci.dci_reports != nil && dci.dci_report_nr > 0 {
		reports := unsafe.Slice(dci.dci_reports, int(dci.dci_report_nr))
		info.Reports = make([]testCheckReportInfo, len(reports))
		for i := range reports {
			r := testCheckReportInfo{
				UUID:   uuidFromC(&reports[i].dcri_uuid),
				Seq:    uint64(reports[i].dcri_seq),
				Class:  uint32(reports[i].dcri_class),
				Action: uint32(reports[i].dcri_act),
				Result: int(reports[i].dcri_result),
			}
			nOpts := int(reports[i].dcri_option_nr)
			r.Options = make([]int, nOpts)
			for j := 0; j < nOpts; j++ {
				r.Options[j] = int(reports[i].dcri_options[j])
			}
			info.Reports[i] = r
		}
	}

	// Free once, assert post-free zeroed, free again (must be a no-op).
	daos_control_check_info_free(&dci)
	info.PostFreeZeroed = dci.dci_pool_nr == 0 && dci.dci_report_nr == 0 &&
		dci.dci_pools == nil && dci.dci_reports == nil &&
		dci.dci_status == nil && dci.dci_phase == nil
	daos_control_check_info_free(&dci)

	return info, rc
}

// callCheckInfoFreeNil exercises the nil-safe path of the free helper.
func callCheckInfoFreeNil() { daos_control_check_info_free(nil) }
