//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

//go:build test_stubs

// cgo drivers for storage_ops_test.go (see test_helpers.go for why these
// can't live in the test file).

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

func callStorageDeviceList(handle cgo.Handle, host string) (ndisks int, rc int) {
	cHost, f := cString(host)
	defer f()
	var n C.int
	rc = int(daos_control_storage_device_list(C.uintptr_t(handle), cHost, &n, nil))
	return int(n), rc
}

// callStorageDeviceListCount runs device_list in count-only mode (devices=NULL)
// with the caller-side *ndisks set to inputCount on entry, returning the value
// the library writes back.
func callStorageDeviceListCount(handle cgo.Handle, host string, inputCount int) (ndisks int, rc int) {
	cHost, f := cString(host)
	defer f()
	n := C.int(inputCount)
	rc = int(daos_control_storage_device_list(C.uintptr_t(handle), cHost, &n, nil))
	return int(n), rc
}

type testDeviceInfo struct {
	UUID    uuid.UUID
	State   string
	Rank    uint32
	Host    string
	Targets []int32
}

func callStorageDeviceListPopulated(handle cgo.Handle, host string, cap int) ([]testDeviceInfo, int, int) {
	cHost, f := cString(host)
	defer f()

	devices := make([]C.struct_device_list, cap)
	n := C.int(cap)
	var devPtr *C.struct_device_list
	if cap > 0 {
		devPtr = &devices[0]
	}
	rc := int(daos_control_storage_device_list(C.uintptr_t(handle), cHost, &n, devPtr))
	if rc != 0 {
		return nil, int(n), rc
	}
	got := int(n)
	if got > cap {
		got = cap
	}
	out := make([]testDeviceInfo, got)
	for i := 0; i < got; i++ {
		tgtCount := int(devices[i].dl_n_tgtidx)
		out[i] = testDeviceInfo{
			UUID:    uuidFromC(&devices[i].dl_device_id),
			State:   C.GoString(&devices[i].dl_state[0]),
			Rank:    uint32(devices[i].dl_rank),
			Host:    C.GoString(&devices[i].dl_host[0]),
			Targets: make([]int32, tgtCount),
		}
		for j := 0; j < tgtCount; j++ {
			out[i].Targets[j] = int32(devices[i].dl_tgtidx[j])
		}
	}
	return out, int(n), rc
}

func callStorageSetNVMeFault(handle cgo.Handle, host string, devUUID uuid.UUID) int {
	cHost, f := cString(host)
	defer f()
	cu := cUUID(devUUID)
	return int(daos_control_storage_set_nvme_fault(C.uintptr_t(handle), cHost, &cu))
}

func callStorageQueryDeviceHealth(handle cgo.Handle, host, statsKey string, devUUID uuid.UUID) (string, int) {
	return callStorageQueryDeviceHealthSized(handle, host, statsKey, devUUID, 256)
}

func callStorageQueryDeviceHealthSized(handle cgo.Handle, host, statsKey string, devUUID uuid.UUID, bufSize int) (string, int) {
	cHost, hF := cString(host)
	defer hF()
	cKey := C.CString(statsKey)
	defer C.free(unsafe.Pointer(cKey))
	cu := cUUID(devUUID)

	out := make([]C.char, bufSize)
	rc := int(daos_control_storage_query_device_health(C.uintptr_t(handle), cHost, cKey, &out[0], C.int(bufSize), &cu))

	var result []byte
	for _, c := range out {
		if c == 0 {
			break
		}
		result = append(result, byte(c))
	}
	return string(result), rc
}
