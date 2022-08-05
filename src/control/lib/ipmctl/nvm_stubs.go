//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build !linux || !amd64
// +build !linux !amd64

// Package ipmctl provides Go bindings for libipmctl Native Management API
package ipmctl

import (
	"github.com/daos-stack/daos/src/control/logging"
)

// DeviceDiscovery struct represents Go equivalent of C.struct_device_discovery
// from nvm_management.h (NVM API) as reported by "go tool cgo -godefs nvm.go"
type DeviceDiscovery struct{}

// Init verifies library version is compatible with this application code.
func (n *NvmMgmt) Init(log logging.Logger) error {
	return nil
}

// GetModules queries number of PMem modules and retrieves device_discovery structs for each before
// converting to Go DeviceDiscovery structs.
func (n *NvmMgmt) GetModules(log logging.Logger) ([]DeviceDiscovery, error) {
	return nil, nil
}

// GetRegions queries number of PMem regions and retrieves region structs for each before
// converting to Go PMemRegion structs.
func (n *NvmMgmt) GetRegions(log logging.Logger) ([]PMemRegion, error) {
	return nil, nil
}

// DeleteConfigGoals removes any pending but not yet applied PMem configuration goals.
func (n *NvmMgmt) DeleteConfigGoals(log logging.Logger) error {
	return nil
}

// GetFirmwareInfo fetches the firmware revision and other information from the device
func (n *NvmMgmt) GetFirmwareInfo(uid DeviceUID) (DeviceFirmwareInfo, error) {
	return nil, nil
}

// UpdateFirmware updates the firmware on the device
func (n *NvmMgmt) UpdateFirmware(uid DeviceUID, fwPath string, force bool) error {
	return nil
}
