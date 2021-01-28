//
// (C) Copyright 2018-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
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
	"os"
	"unsafe"

	"github.com/pkg/errors"
)

// IpmCtl is the interface that provides access to libipmctl.
type IpmCtl interface {
	// Discover persistent memory modules
	Discover() ([]DeviceDiscovery, error)
	// Get firmware information from persistent memory modules
	GetFirmwareInfo(uid DeviceUID) (DeviceFirmwareInfo, error)
	// Update persistent memory module firmware
	UpdateFirmware(uid DeviceUID, fwPath string, force bool) error
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
	if rc != C.NVM_SUCCESS && rc != C.NVM_SUCCESS_FW_RESET_REQUIRED {
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
	if len(fwPath) == 0 {
		return errors.New("firmware path is required")
	}

	if _, err := os.Stat(fwPath); err != nil {
		return errors.Wrap(err, "unable to access firmware file")
	}

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
