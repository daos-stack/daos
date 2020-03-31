//
// (C) Copyright 2019 Intel Corporation.
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
	}

	// ScmModules is a type alias for []ScmModule that implements fmt.Stringer.
	ScmModules []*ScmModule

	// ScmNamespace represents a mapping of AppDirect regions to block device files.
	ScmNamespace struct {
		UUID        string `json:"uuid"`
		BlockDevice string `json:"blockdev"`
		Name        string `json:"dev"`
		NumaNode    uint32 `json:"numa_node"`
		Size        uint64 `json:"size"`
	}

	// ScmNamespaces is a type alias for []ScmNamespace that implements fmt.Stringer.
	ScmNamespaces []*ScmNamespace

	// NvmeDeviceHealth represents a set of health statistics for a NVMe device.
	NvmeDeviceHealth struct {
		Temp            uint32
		TempWarnTime    uint32
		TempCritTime    uint32
		CtrlBusyTime    uint64
		PowerCycles     uint64
		PowerOnHours    uint64
		UnsafeShutdowns uint64
		MediaErrors     uint64
		ErrorLogEntries uint64
		TempWarn        bool
		AvailSpareWarn  bool
		ReliabilityWarn bool
		ReadOnlyWarn    bool
		VolatileWarn    bool
	}

	// NvmeNamespace represents an individual NVMe namespace on a device.
	NvmeNamespace struct {
		ID   uint32
		Size uint64
	}

	// NvmeController represents a NVMe device controller which includes health
	// and namespace information.
	NvmeController struct {
		Model       string
		Serial      string
		PciAddr     string
		FwRev       string
		SocketID    int32
		HealthStats *NvmeDeviceHealth
		Namespaces  []*NvmeNamespace
	}

	// NvmeControllers is a type alias for []*NvmeController which implements fmt.Stringer.
	NvmeControllers []*NvmeController
)

func (sm *ScmModule) String() string {
	// capacity given in IEC standard units.
	return fmt.Sprintf("PhysicalID:%d Capacity:%s Location:(socket:%d memctrlr:%d "+
		"chan:%d pos:%d)", sm.PhysicalID, humanize.IBytes(sm.Capacity),
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
