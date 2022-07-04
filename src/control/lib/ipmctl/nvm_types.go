//
// (C) Copyright 2018-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ipmctl

import "bytes"

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
		RegionHealthNormal:  "Normal",
		RegionHealthError:   "Error",
		RegionHealthUnknown: "Unknown",
		RegionHealthPending: "Pending",
		RegionHealthLocked:  "Locked",
	}[pmrh]
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
