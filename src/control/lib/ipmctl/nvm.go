//
// (C) Copyright 2018-2022 Intel Corporation.
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

	"github.com/daos-stack/daos/src/control/logging"
)

const NvmVersionMajor = 2

// Rc2err returns an failure if rc != NVM_SUCCESS.
//
// TODO: print human readable error with provided lib macros
func Rc2err(label string, rc C.int) error {
	if rc != C.NVM_SUCCESS && rc != C.NVM_SUCCESS_FW_RESET_REQUIRED {
		return fmt.Errorf("%s: rc=%d", label, int(rc))
	}
	return nil
}

type (
	// IpmCtl is the interface that provides access to libipmctl.
	IpmCtl interface {
		// GetModules discovers persistent memory modules.
		GetModules() ([]DeviceDiscovery, error)
		// GetRegions discovers persistent memory regions.
		GetRegions(logging.Logger) ([]PMemRegion, error)
		// GetFirmwareInfo retrieves firmware information from persistent memory modules.
		GetFirmwareInfo(uid DeviceUID) (DeviceFirmwareInfo, error)
		// UpdateFirmware updates persistent memory module firmware.
		UpdateFirmware(uid DeviceUID, fwPath string, force bool) error
	}

	// NvmMgmt is an implementation of the IpmCtl interface which exercises
	// libipmctl's NVM API.
	NvmMgmt struct{}

	getNumberOfDevicesFn func(*C.uint) C.int
	getDevicesFn         func(*C.struct_device_discovery, C.NVM_UINT8) C.int
	getNumberOfRegionsFn func(*C.NVM_UINT8) C.int
	getRegionsFn         func(*C.struct_region, *C.NVM_UINT8) C.int
)

func getNumberOfDevices(out *C.uint) C.int {
	return C.nvm_get_number_of_devices(out)
}

func getDevices(devs *C.struct_device_discovery, count C.NVM_UINT8) C.int {
	return C.nvm_get_devices(devs, count)
}

func getModules(getNumDevs getNumberOfDevicesFn, getDevs getDevicesFn) (devices []DeviceDiscovery, err error) {
	var count C.uint
	if err = Rc2err("get_number_of_devices", getNumDevs(&count)); err != nil {
		return
	}
	if count == 0 {
		return
	}

	devs := make([]C.struct_device_discovery, int(count))

	if err = Rc2err("get_devices", getDevs(&devs[0], C.NVM_UINT8(count))); err != nil {
		return
	}

	// Cast struct array to slice of go equivalent structs.
	devices = (*[1 << 30]DeviceDiscovery)(unsafe.Pointer(&devs[0]))[:count:count]
	if len(devices) != int(count) {
		err = fmt.Errorf("expected %d devices but got %d", int(count), len(devices))
	}

	return
}

func checkVersion() error {
	gotVer := C.nvm_get_major_version()

	if int(gotVer) != NvmVersionMajor {
		return errors.Errorf("libipmctl version mismatch, want major version %d but got %d",
			NvmVersionMajor, gotVer)
	}

	return nil
}

// GetModules queries number of PMem modules and retrieves device_discovery structs for each before
// converting to Go DeviceDiscovery structs.
func (n *NvmMgmt) GetModules() (devices []DeviceDiscovery, err error) {
	return getModules(getNumberOfDevices, getDevices)
}

func getNumberOfPMemRegions(out *C.NVM_UINT8) C.int {
	return C.nvm_get_number_of_regions(out)
}

func getPMemRegions(regions *C.struct_region, count *C.NVM_UINT8) C.int {
	return C.nvm_get_regions(regions, count)
}

func getRegions(log logging.Logger, getNum getNumberOfRegionsFn, get getRegionsFn) (regions []PMemRegion, err error) {
	var count C.NVM_UINT8
	if err = Rc2err("get_number_of_regions", getNum(&count)); err != nil {
		return
	}
	if count == 0 {
		return
	}

	pmemRegions := make([]C.struct_region, int(count))

	if err = Rc2err("get_regions", get(&pmemRegions[0], &count)); err != nil {
		return
	}
	for idx, pmr := range pmemRegions {
		log.Debugf("ipmctl pmem region %d: %+v", idx, pmr)
	}

	// Cast struct array to slice of go equivalent structs.
	regions = (*[1 << 30]PMemRegion)(unsafe.Pointer(&pmemRegions[0]))[:count:count]
	if len(regions) != int(count) {
		err = fmt.Errorf("expected %d regions but got %d", int(count), len(regions))
	}

	return
}

// GetRegions queries number of PMem regions and retrieves region structs for each before
// converting to Go PMemRegion structs.
func (n *NvmMgmt) GetRegions(log logging.Logger) (regions []PMemRegion, err error) {
	return getRegions(log, getNumberOfPMemRegions, getPMemRegions)
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
