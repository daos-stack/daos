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

package ipmctl

import "bytes"

// DeviceUID represents the Go equivalent of an NVM_UID string buffer
type DeviceUID [22]byte

// String converts the DeviceUID bytes to a string
func (d DeviceUID) String() string {
	n := bytes.IndexByte(d[:], 0)
	return string(d[:n])
}

// Version represents the Go equivalent of an NVM_VERSION string buffer
type Version [25]byte

// String converts the Version bytes to a string
func (v Version) String() string {
	n := bytes.IndexByte(v[:], 0)
	return string(v[:n])
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
	Part_number              [21]int8
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

// DeviceFirmwareInfo represents an ipmctl device_fw_info structure
type DeviceFirmwareInfo struct {
	ActiveFWVersion Version
	StagedFWVersion Version
	FWImageMaxSize  uint32
	FWUpdateStatus  uint32
	Reserved        [4]uint8
}
