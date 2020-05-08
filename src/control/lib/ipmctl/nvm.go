//
// (C) Copyright 2018-2020 Intel Corporation.
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
	// Get firmware information from persistent memory modules
	GetFirmwareInfo(uid DeviceUID) (DeviceFirmwareInfo, error)
	// Update persistent memory module firmware
	UpdateFirmware(uid DeviceUID, fwPath string, force bool) error
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
	if err = Rc2err(
		"get_number_of_devices",
		C.nvm_get_number_of_devices(&count)); err != nil {
		return
	}
	if count == 0 {
		return
	}

	devs := make([]C.struct_device_discovery, int(count))
	// println(len(devs))

	// don't need to defer free on devs as we allocated in go
	if err = Rc2err(
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

// Rc2err returns an failure if rc != NVM_SUCCESS.
//
// TODO: print human readable error with provided lib macros
func Rc2err(label string, rc C.int) error {
	if rc != C.NVM_SUCCESS {
		return fmt.Errorf("%s: rc=%d", label, int(rc))
	}
	return nil
}

// GetFirmwareInfo fetches the firmware revision and other information from the device
func (n *NvmMgmt) GetFirmwareInfo(uid DeviceUID) (fw DeviceFirmwareInfo, err error) {
	cUID := C.CString(uid.String())
	cInfo := new(C.struct_device_fw_info)

	if err = Rc2err(
		"get_device_fw_info",
		C.nvm_get_device_fw_image_info(cUID, cInfo)); err != nil {
		return
	}

	fw = *(*DeviceFirmwareInfo)(unsafe.Pointer(cInfo))
	return
}

// UpdateFirmware updates the firmware on the device
func (n *NvmMgmt) UpdateFirmware(uid DeviceUID, fwPath string, force bool) error {
	cUID := C.CString(uid.String())
	cPath := C.CString(fwPath)
	cPathLen := C.ulong(len(fwPath))
	var cForce C.uchar
	if force {
		cForce = 1
	}

	if err := Rc2err(
		"update_device_fw",
		C.nvm_update_device_fw(cUID, cPath, cPathLen, cForce)); err != nil {
		return err
	}

	return nil
}
