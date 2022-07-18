//
// (C) Copyright 2018-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build !linux || !amd64
// +build !linux !amd64

// Package ipmctl provides Go bindings for libipmctl Native Management API
package ipmctl

// CGO_CFLAGS & CGO_LDFLAGS env vars can be used
// to specify additional dirs.

/*

#include "nvm_management.h"
*/
import "C"

import (
	"github.com/daos-stack/daos/src/control/logging"
)

// Init verifies library version is compatible with this application code.
func (n *NvmMgmt) Init(log logging.Logger) error {
	return nil
}

func getDevices(log logging.Logger, devs *C.struct_device_discovery, count C.NVM_UINT8) C.int {
	return 0
}

// GetModules queries number of PMem modules and retrieves device_discovery structs for each before
// converting to Go DeviceDiscovery structs.
func (n *NvmMgmt) GetModules(log logging.Logger) (devices []DeviceDiscovery, err error) {
	return
}

// GetRegions queries number of PMem regions and retrieves region structs for each before
// converting to Go PMemRegion structs.
func (n *NvmMgmt) GetRegions(log logging.Logger) (regions []PMemRegion, err error) {
	return
}

// DeleteConfigGoals removes any pending but not yet applied PMem configuration goals.
func (n *NvmMgmt) DeleteConfigGoals(log logging.Logger) error {
	return nil
}

// GetFirmwareInfo fetches the firmware revision and other information from the device
func (n *NvmMgmt) GetFirmwareInfo(uid DeviceUID) (fw DeviceFirmwareInfo, err error) {
	return
}

// UpdateFirmware updates the firmware on the device
func (n *NvmMgmt) UpdateFirmware(uid DeviceUID, fwPath string, force bool) error {
	return nil
}
