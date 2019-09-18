//
// (C) Copyright 2018-2019 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

// Package ipmctl provides Go bindings for libipmctl Native Management API
package ipmctl

// CGO_CFLAGS & CGO_LDFLAGS env vars can be used
// to specify additional dirs.

/*
#cgo LDFLAGS: -lipmctl

#include "stdlib.h"
#include "nvm_management.h"
#include "nvm_types.h"
#include "NvmSharedDefs.h"
#include "export_api.h"
*/
import "C"

import (
	"fmt"
	"unsafe"
)

// IpmCtl is the interface that provides access to libipmctl.
type IpmCtl interface {
	// SetInterleaved mode for app-direct regions
	// process referred to as "set goal" in NVM API
	//SetRegion(...)
	// Discover persistent memory modules
	Discover() ([]DeviceDiscovery, error)
	// Update persistent memory module firmware
	//Update(...)
	// Cleanup persistent memory references
	//Cleanup()
}

// NvmMgmt is an implementation of the IpmCtl interface which exercises
// libipmctl's NVM API.
type NvmMgmt struct{}

// Discover queries number of SCM modules and retrieves device_discovery structs
// for each.
func (n *NvmMgmt) Discover() (devices []DeviceDiscovery, err error) {
	var count C.uint
	if err = rc2err(
		"get_number_of_devices",
		C.nvm_get_number_of_devices(&count)); err != nil {
		return
	}
	if count == 0 {
		println("no NVDIMMs found!")
		return
	}

	devs := make([]C.struct_device_discovery, int(count))
	// println(len(devs))

	// don't need to defer free on devs as we allocated in go
	if err = rc2err(
		"get_devices",
		C.nvm_get_devices(&devs[0], C.NVM_UINT8(count))); err != nil {
		return
	}
	// defer C.free(unsafe.Pointer(&devs))

	// cast struct array to slice of go equivalent struct
	// (get equivalent go struct def from cgo -godefs)
	devices = (*[1 << 30]DeviceDiscovery)(unsafe.Pointer(&devs[0]))[:count:count]
	if len(devices) != int(count) {
		err = fmt.Errorf("expected %d devices but got %d", len(devices), int(count))
	}

	return
}

// GetStatuses return status for each device in devices
func (n *NvmMgmt) GetStatuses(devices []DeviceDiscovery) (
	statuses []DeviceStatus, err error) {

	// printing for debug
	for i, d := range devices {
		fmt.Printf("Device ID: %d, Memory type: %d, Fw Rev: %v, Capacity %d, ",
			d.Device_id, d.Memory_type, d.Fw_revision, d.Capacity)
		fmt.Printf("Channel Pos: %d, Channel ID: %d, Memory Ctrlr: %d, Socket ID: %d.\n",
			d.Channel_pos, d.Channel_id, d.Memory_controller_id, d.Socket_id)

		uidCharPtr := (*C.char)(unsafe.Pointer(&devices[0].Uid))
		//uidCharPtr := (*C.char)(unsafe.Pointer(&devs[0].uid))

		fmt.Printf("uid of device %d: %s\n", i, C.GoString(uidCharPtr))

		status := C.struct_device_status{}
		if err = rc2err(
			"get_device_status",
			C.nvm_get_device_status(uidCharPtr, &status)); err != nil {
			return
		}
		statuses = append(statuses, *(*DeviceStatus)(unsafe.Pointer(&status)))
	}

	// verify api call passing in uid as param
	// dev := C.struct_device_discovery{}
	// C.nvm_get_device_discovery(uidCharPtr, &dev)
	// dd := (*DeviceDiscovery)(unsafe.Pointer(&dev))
	// fmt.Printf("Device ID: %d, Memory type: %d, Fw Rev: %v, Capacity %d, ",
	//    dd.Device_id, dd.Memory_type, dd.Fw_revision, dd.Capacity)

	return
}

// rc2err returns an failure if rc != NVM_SUCCESS.
//
// TODO: print human readable error with provided lib macros
func rc2err(label string, rc C.int) error {
	if rc != C.NVM_SUCCESS {
		// e := errors.Error(C.NVDIMM_ERR_W(FORMAT_STR_NL, rc))
		return fmt.Errorf("%s: rc=%d", label, int(rc)) // e
	}
	return nil
}

// example unit test from NVM API source
//TEST_F(NvmApi_Tests, GetDeviceStatus)
//{
//  unsigned int dimm_cnt = 0;
//  nvm_get_number_of_devices(&dimm_cnt);
//  device_discovery *p_devices = (device_discovery *)malloc(sizeof(device_discovery) * dimm_cnt);
//  nvm_get_devices(p_devices, dimm_cnt);
//  device_status *p_status = (device_status *)malloc(sizeof(device_status));
//  EXPECT_EQ(nvm_get_device_status(p_devices->uid, p_status), NVM_SUCCESS);
//  free(p_status);
//  free(p_devices);
//}
