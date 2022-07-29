//
// (C) Copyright 2018-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build linux && amd64
// +build linux,amd64

package ipmctl

/*
#cgo LDFLAGS: -lipmctl

#include "stdlib.h"
#include "nvm_management.h"
#include "nvm_types.h"
#include "NvmSharedDefs.h"
#include "export_api.h"

static void setArrayDevice(struct device_discovery *p_devices, int n) {
	struct device_discovery *ptr;

	ptr = p_devices + (sizeof(struct device_discovery) * n);
	ptr->device_id = (n+1) * 1111;
	ptr->vendor_id = (n+1) * 1111;
	ptr->physical_id = (n+1) * 1111;
	ptr->revision_id = (n+1) * 1111;
}
*/
import "C"

import (
	"testing"
	"unsafe"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

func testGetModules(t *testing.T) {
	for name, tc := range map[string]struct {
		getNumRC   C.int
		getNumRet  C.uint
		getRC      C.int
		expModules []DeviceDiscovery
		expErr     error
	}{
		"get number fails": {
			getNumRC: -1,
			expErr:   errors.New("get_number_of_devices: rc=-1"),
		},
		"no modules": {},
		"get devices fails": {
			getNumRet: 1,
			getRC:     -1,
			expErr:    errors.New("get_devices: rc=-1"),
		},
		"get devices succeeds": {
			getNumRet: 1,
			expModules: []DeviceDiscovery{
				{Device_id: 1111},
			},
		},
		"get devices succeeds; multiple": {
			getNumRet: 3,
			expModules: []DeviceDiscovery{
				{Device_id: 1111},
				{Device_id: 2222},
				{Device_id: 3333},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			mockGetNum := func(_ logging.Logger, out *C.uint) C.int {
				*out = tc.getNumRet
				return tc.getNumRC
			}

			mockGet := func(_ logging.Logger, devs *C.struct_device_discovery, count C.NVM_UINT8) C.int {
				if int(count) != int(tc.getNumRet) {
					t.Fatal("device count does not match")
				}
				if tc.getRC != 0 {
					return tc.getRC
				}
				const size = unsafe.Sizeof(C.struct_device_discovery{})
				for i := 0; i < int(count); i++ {
					// Update field values in C struct array:
					// - calc ptr offset for desired array struct
					// - cast unsafe ptr to struct ptr type and dereference
					// - update struct field value
					(*(*C.struct_device_discovery)(unsafe.Pointer(
						uintptr(unsafe.Pointer(devs)) + (uintptr(i) * size),
					))).device_id = C.NVM_UINT16((i + 1) * 1111)
				}
				return 0
			}

			modules, err := getModules(log, mockGetNum, mockGet)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expModules, modules,
				cmpopts.IgnoreFields(DeviceDiscovery{}, "Security_capabilities",
					"Device_capabilities")); diff != "" {
				t.Fatalf("Unexpected modules output (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func testGetRegions(t *testing.T) {
	for name, tc := range map[string]struct {
		getNumRC   C.int
		getNumRet  C.NVM_UINT8
		getRC      C.int
		expRegions []PMemRegion
		expErr     error
	}{
		"get number fails": {
			getNumRC: -1,
			expErr:   errors.New("get_number_of_regions: rc=-1"),
		},
		"no regions": {},
		"get regions fails": {
			getNumRet: 1,
			getRC:     -1,
			expErr:    errors.New("get_regions: rc=-1"),
		},
		"get regions succeeds": {
			getNumRet: 1,
			expRegions: []PMemRegion{
				{Capacity: 2222, Free_capacity: 1111, Socket_id: 1, Dimm_count: 1},
			},
		},
		"get regions succeeds; multiple": {
			getNumRet: 3,
			expRegions: []PMemRegion{
				{Capacity: 2222, Free_capacity: 1111, Socket_id: 1, Dimm_count: 1},
				{Capacity: 4444, Free_capacity: 2222, Socket_id: 0, Dimm_count: 2},
				{Capacity: 6666, Free_capacity: 3333, Socket_id: 1, Dimm_count: 3},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			mockGetNum := func(_ logging.Logger, out *C.NVM_UINT8) C.int {
				*out = tc.getNumRet
				return tc.getNumRC
			}

			mockGet := func(_ logging.Logger, regions *C.struct_region, count *C.NVM_UINT8) C.int {
				if int(*count) != int(tc.getNumRet) {
					t.Fatal("device count does not match")
				}
				if tc.getRC != 0 {
					return tc.getRC
				}
				const size = unsafe.Sizeof(C.struct_region{})
				for i := 0; i < int(*count); i++ {
					// Update field values in C struct array:
					// - calc ptr offset for desired array struct
					// - cast to struct ptr type and dereference
					// - update struct field value
					(*(*C.struct_region)(unsafe.Pointer(
						uintptr(unsafe.Pointer(regions)) + (uintptr(i) * size),
					))).capacity = C.NVM_UINT64((i + 1) * 2222)
					(*(*C.struct_region)(unsafe.Pointer(
						uintptr(unsafe.Pointer(regions)) + (uintptr(i) * size),
					))).free_capacity = C.NVM_UINT64((i + 1) * 1111)
					(*(*C.struct_region)(unsafe.Pointer(
						uintptr(unsafe.Pointer(regions)) + (uintptr(i) * size),
					))).socket_id = C.NVM_INT16((i + 1) % 2)
					(*(*C.struct_region)(unsafe.Pointer(
						uintptr(unsafe.Pointer(regions)) + (uintptr(i) * size),
					))).dimm_count = C.NVM_UINT16(i + 1)
				}
				return 0
			}

			regions, err := getRegions(log, mockGetNum, mockGet)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expRegions, regions); diff != "" { //,
				t.Fatalf("Unexpected regions output (-want, +got):\n%s\n", diff)
			}
		})
	}
}
