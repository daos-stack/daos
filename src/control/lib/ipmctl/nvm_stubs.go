//
// (C) Copyright 2018-2022 Intel Corporation.
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

// DeviceDiscovery struct stub defined without CGO type members.
type DeviceDiscovery struct {
	All_properties_populated uint8
	Pad_cgo_0                [3]byte
	Device_handle            [4]byte
	Physical_id              uint16
	Vendor_id                uint16
	Device_id                uint16
	Revision_id              uint16
	Channel_pos              uint16
	Channel_id               uint16
	Memory_controller_id     uint16
	Socket_id                uint16
	Node_controller_id       uint16
	Pad_cgo_1                [2]byte
	Memory_type              uint32
	Dimm_sku                 uint32
	Manufacturer             [2]uint8
	Serial_number            [4]uint8
	Subsystem_vendor_id      uint16
	Subsystem_device_id      uint16
	Subsystem_revision_id    uint16
	Manufacturing_info_valid uint8
	Manufacturing_location   uint8
	Manufacturing_date       uint16
	Part_number              PartNumber
	Fw_revision              Version
	Fw_api_version           Version
	Pad_cgo_2                [5]byte
	Capacity                 uint64
	Interface_format_codes   [9]uint16
	Uid                      DeviceUID
	Lock_state               uint32
	Manageability            uint32
	Controller_revision_id   uint16
	Reserved                 [48]uint8
	Pad_cgo_3                [6]byte
}

// Init verifies library version is compatible with this application code.
func (n *NvmMgmt) Init(log logging.Logger) error {
	return nil
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
