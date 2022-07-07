//
// (C) Copyright 2018-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ipmctl

import (
	"bytes"
)

// DeviceUID represents the Go equivalent of an NVM_UID string buffer
type DeviceUID [22]byte

// String converts the DeviceUID bytes to a string
func (d DeviceUID) String() string {
	return bytes2String(d[:])
}

func bytes2String(b []byte) string {
	n := bytes.IndexByte(b, 0)
	return string(b[:n])
}

// Version represents the Go equivalent of an NVM_VERSION string buffer
type Version [25]byte

// String converts the Version bytes to a string
func (v Version) String() string {
	return bytes2String(v[:])
}

// PartNumber represents the part number string for an NVM device.
type PartNumber [21]byte

// String converts the PartNumber bytes to a string
func (p PartNumber) String() string {
	return bytes2String(p[:])
}

// DeviceDiscovery struct represents Go equivalent of C.struct_device_discovery
// from nvm_management.h (NVM API) as reported by "go tool cgo -godefs nvm.go"
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
	Security_capabilities    _Ctype_struct_device_security_capabilities
	Device_capabilities      _Ctype_struct_device_capabilities
	Uid                      DeviceUID
	Lock_state               uint32
	Manageability            uint32
	Controller_revision_id   uint16
	Reserved                 [48]uint8
	Pad_cgo_3                [6]byte
}

// PMemRegionType represents PMem region type.
type PMemRegionType uint32

// PMemRegionType values represent the ipmctl region_type enum. Type of region.
const (
	RegionTypeUnknown        PMemRegionType = iota
	RegionTypeAppDirect                     // App Direct mode.
	RegionTypeNotInterleaved                // Non-interleaved App Direct mode.
	RegionTypeVolatile                      // Volatile.
)

func (pmrt PMemRegionType) String() string {
	return map[PMemRegionType]string{
		RegionTypeUnknown:        "Unknown",
		RegionTypeAppDirect:      "AppDirect",
		RegionTypeNotInterleaved: "AppDirectNotInterleaved",
		RegionTypeVolatile:       "Volatile",
	}[pmrt]
}

func PMemRegionTypeFromString(in string) PMemRegionType {
	return map[string]PMemRegionType{
		"Unknown":                 RegionTypeUnknown,
		"AppDirect":               RegionTypeAppDirect,
		"AppDirectNotInterleaved": RegionTypeNotInterleaved,
		"Volatile":                RegionTypeVolatile,
	}[in]
}

// PMemRegionHealth represents PMem region health.
type PMemRegionHealth uint32

// PMemRegionHealth values represent the ipmctl region_health enum. Rolled-up health of the underlying
// PMem modules from which the REGION is created. Constant values start at 1.
const (
	_                   PMemRegionHealth = iota
	RegionHealthNormal                   // All underlying PMem module capacity is available.
	RegionHealthError                    // Issue with some or all of the underlying PMem module capacity.
	RegionHealthUnknown                  // The region health cannot be determined.
	RegionHealthPending                  // A new memory allocation goal has been created but not applied.
	RegionHealthLocked                   // One or more of the underlying PMem modules are locked.
)

func (pmrh PMemRegionHealth) String() string {
	return map[PMemRegionHealth]string{
		RegionHealthNormal:  "Healthy",
		RegionHealthError:   "Error",
		RegionHealthUnknown: "Unknown",
		RegionHealthPending: "Pending",
		RegionHealthLocked:  "Locked",
	}[pmrh]
}

func PMemRegionHealthFromString(in string) PMemRegionHealth {
	return map[string]PMemRegionHealth{
		"Healthy": RegionHealthNormal,
		"Error":   RegionHealthError,
		"Unknown": RegionHealthUnknown,
		"Pending": RegionHealthPending,
		"Locked":  RegionHealthLocked,
	}[in]
}

// PMemRegion represents Go equivalent of C.struct_region from nvm_management.h (NVM API) as
// reported by "go tool cgo -godefs nvm.go".
type PMemRegion struct {
	IsetId        uint64     // Unique identifier of the region.
	Type          uint32     // The type of region.
	Capacity      uint64     // Size of the region in bytes.
	Free_capacity uint64     // Available size of the region in bytes.
	Socket_id     int16      // socket ID
	Dimm_count    uint16     // The number of PMem modules in this region.
	Dimms         [24]uint16 // Unique ID's of underlying PMem modules.
	Health        uint32     // Rolled up health of the underlying PMem modules.
	Reserved      [40]uint8  // reserved
}

// FWUpdateStatus values represent the ipmctl fw_update_status enum
const (
	// FWUpdateStatusUnknown represents unknown status
	FWUpdateStatusUnknown = 0
	// FWUpdateStatusStaged represents a staged FW update to be loaded on reboot
	FWUpdateStatusStaged = 1
	// FWUpdateStatusSuccess represents a successfully applied FW update
	FWUpdateStatusSuccess = 2
	// FWUpdateStatusFailed represents a failed FW update
	FWUpdateStatusFailed = 3
)

// DeviceFirmwareInfo represents an ipmctl device_fw_info structure
type DeviceFirmwareInfo struct {
	ActiveFWVersion Version // currently running FW version
	StagedFWVersion Version // FW version to be applied on next reboot
	FWImageMaxSize  uint32  // maximum FW image size in 4096-byte chunks
	FWUpdateStatus  uint32  // last update status
	Reserved        [4]uint8
}
