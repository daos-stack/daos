//
// (C) Copyright 2019-2020 Intel Corporation.
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

package storage

import (
	"bytes"
	"fmt"
	"sort"

	"github.com/dustin/go-humanize"

	"github.com/daos-stack/daos/src/control/common"
)

// ScmState represents the probed state of SCM modules on the system.
//go:generate stringer -type=ScmState
type ScmState int

const (
	// ScmStateUnknown represents the default (unknown) state.
	ScmStateUnknown ScmState = iota
	// ScmStateNoRegions indicates that SCM modules exist, but
	// no regions have been created.
	ScmStateNoRegions
	// ScmStateFreeCapacity indicates that SCM modules exist with
	// configured regions that have available capacity.
	ScmStateFreeCapacity
	// ScmStateNoCapacity indicates that SCM modules exist with
	// configured regions but not available capacity.
	ScmStateNoCapacity
)

type (
	// ScmModule represents a SCM DIMM.
	//
	// This is a simplified representation of the raw struct used in the ipmctl package.
	ScmModule struct {
		ChannelID       uint32
		ChannelPosition uint32
		ControllerID    uint32
		SocketID        uint32
		PhysicalID      uint32
		Capacity        uint64
		UID             string
	}

	// ScmModules is a type alias for []ScmModule that implements fmt.Stringer.
	ScmModules []*ScmModule

	// ScmNamespace represents a mapping of AppDirect regions to block device files.
	ScmNamespace struct {
		UUID        string `json:"uuid" hash:"ignore"`
		BlockDevice string `json:"blockdev"`
		Name        string `json:"dev"`
		NumaNode    uint32 `json:"numa_node"`
		Size        uint64 `json:"size"`
	}

	// ScmNamespaces is a type alias for []ScmNamespace that implements fmt.Stringer.
	ScmNamespaces []*ScmNamespace

	ScmMountPoint struct {
		Info string
		Path string
	}

	ScmMountPoints []*ScmMountPoint

	// ScmFirmwareUpdateStatus represents the status of a firmware update on the module.
	ScmFirmwareUpdateStatus uint32

	// ScmFirmwareInfo describes the firmware information of an SCM module.
	ScmFirmwareInfo struct {
		ActiveVersion     string
		StagedVersion     string
		ImageMaxSizeBytes uint32
		UpdateStatus      ScmFirmwareUpdateStatus
	}

	// NvmeControllerHealth represents a set of health statistics for a NVMe device
	// and mirrors C.struct_nvme_health_stats.
	NvmeControllerHealth struct {
		Model           string `json:"model"`
		Serial          string `json:"serial"`
		Timestamp       uint64 `json:"timestamp"`
		ErrorCount      uint64 `json:"err_count"`
		TempWarnTime    uint32 `json:"warn_temp_time"`
		TempCritTime    uint32 `json:"crit_temp_time"`
		CtrlBusyTime    uint64 `json:"ctrl_busy_time"`
		PowerCycles     uint64 `json:"power_cycles"`
		PowerOnHours    uint64 `json:"power_on_hours"`
		UnsafeShutdowns uint64 `json:"unsafe_shutdowns"`
		MediaErrors     uint64 `json:"media_errs"`
		ErrorLogEntries uint64 `json:"err_log_entries"`
		ReadErrors      uint32 `json:"bio_read_errs"`
		WriteErrors     uint32 `json:"bio_write_errs"`
		UnmapErrors     uint32 `json:"bio_unmap_errs"`
		ChecksumErrors  uint32 `json:"checksum_errs"`
		Temperature     uint32 `json:"temperature"`
		TempWarn        bool   `json:"temp_warn"`
		AvailSpareWarn  bool   `json:"avail_spare_warn"`
		ReliabilityWarn bool   `json:"dev_reliability_warn"`
		ReadOnlyWarn    bool   `json:"read_only_warn"`
		VolatileWarn    bool   `json:"volatile_mem_warn"`
	}

	// NvmeNamespace represents an individual NVMe namespace on a device and
	// mirrors C.struct_ns_t.
	NvmeNamespace struct {
		ID   uint32
		Size uint64
	}

	// NvmeController represents a NVMe device controller which includes health
	// and namespace information and mirrors C.struct_ns_t.
	NvmeController struct {
		Info        string
		Model       string
		Serial      string `hash:"ignore"`
		PciAddr     string
		FwRev       string
		SocketID    int32
		HealthStats *NvmeControllerHealth `hash:"ignore"`
		Namespaces  []*NvmeNamespace
	}

	// NvmeControllers is a type alias for []*NvmeController which implements fmt.Stringer.
	NvmeControllers []*NvmeController
)

const (
	// ScmUpdateStatusUnknown indicates that the firmware update status is unknown.
	ScmUpdateStatusUnknown ScmFirmwareUpdateStatus = iota
	// ScmUpdateStatusStaged indicates that a new firmware version has been staged.
	ScmUpdateStatusStaged
	// ScmUpdateStatusSuccess indicates that the firmware update was successfully applied.
	ScmUpdateStatusSuccess
	// ScmUpdateStatusFailed indicates that the firmware update failed.
	ScmUpdateStatusFailed
)

// String translates the update status to a string
func (s ScmFirmwareUpdateStatus) String() string {
	switch s {
	case ScmUpdateStatusStaged:
		return "Staged"
	case ScmUpdateStatusSuccess:
		return "Success"
	case ScmUpdateStatusFailed:
		return "Failed"
	}
	return "Unknown"
}

func (ndh *NvmeControllerHealth) TempK() uint32 {
	return uint32(ndh.Temperature)
}

func (ndh *NvmeControllerHealth) TempC() float32 {
	return float32(ndh.Temperature) - 273.15
}

func (ndh *NvmeControllerHealth) TempF() float32 {
	return ndh.TempC()*(9/5) + 32
}

func (sm *ScmModule) String() string {
	// capacity given in IEC standard units.
	return fmt.Sprintf("UID:%s PhysicalID:%d Capacity:%s Location:(socket:%d memctrlr:%d "+
		"chan:%d pos:%d)", sm.UID, sm.PhysicalID, humanize.IBytes(sm.Capacity),
		sm.SocketID, sm.ControllerID, sm.ChannelID, sm.ChannelPosition)
}

func (sms ScmModules) String() string {
	var buf bytes.Buffer

	if len(sms) == 0 {
		return "\t\tnone\n"
	}

	sort.Slice(sms, func(i, j int) bool { return sms[i].PhysicalID < sms[j].PhysicalID })

	for _, sm := range sms {
		fmt.Fprintf(&buf, "\t\t%s\n", sm)
	}

	return buf.String()
}

// Capacity reports total storage capacity (bytes) across all modules.
func (sms ScmModules) Capacity() (tb uint64) {
	for _, sm := range sms {
		tb += sm.Capacity
	}
	return
}

// Summary reports total storage space and the number of modules.
//
// Capacity given in IEC standard units.
func (sms ScmModules) Summary() string {
	return fmt.Sprintf("%s (%d %s)", humanize.IBytes(sms.Capacity()), len(sms),
		common.Pluralise("module", len(sms)))
}

func (sn *ScmNamespace) String() string {
	// capacity given in IEC standard units.
	return fmt.Sprintf("Device:%s Socket:%d Capacity:%s", sn.BlockDevice, sn.NumaNode,
		humanize.Bytes(sn.Size))
}

func (sns ScmNamespaces) String() string {
	var buf bytes.Buffer

	if len(sns) == 0 {
		return "\t\tnone\n"
	}

	sort.Slice(sns, func(i, j int) bool { return sns[i].BlockDevice < sns[j].BlockDevice })

	for _, sn := range sns {
		fmt.Fprintf(&buf, "\t\t%s\n", sn)
	}

	return buf.String()
}

// Capacity reports total storage capacity (bytes) across all namespaces.
func (sns ScmNamespaces) Capacity() (tb uint64) {
	for _, sn := range sns {
		tb += sn.Size
	}
	return
}

// Summary reports total storage space and the number of namespaces.
//
// Capacity given in IEC standard units.
func (sns ScmNamespaces) Summary() string {
	return fmt.Sprintf("%s (%d %s)", humanize.Bytes(sns.Capacity()), len(sns),
		common.Pluralise("namespace", len(sns)))
}

func (nc *NvmeController) Capacity() (tb uint64) {
	for _, n := range nc.Namespaces {
		tb += n.Size
	}
	return
}

func (ncs NvmeControllers) Capacity() (tb uint64) {
	for _, c := range ncs {
		tb += (*NvmeController)(c).Capacity()
	}
	return
}

// Summary reports accumulated storage space and the number of controllers.
func (ncs NvmeControllers) Summary() string {
	return fmt.Sprintf("%s (%d %s)", humanize.Bytes(ncs.Capacity()),
		len(ncs), common.Pluralise("controller", len(ncs)))
}
