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

	bytesize "github.com/inhies/go-bytesize"

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
	ScmModules []ScmModule

	// ScmNamespace represents a mapping of AppDirect regions to block device files.
	ScmNamespace struct {
		UUID        string `json:"uuid"`
		BlockDevice string `json:"blockdev"`
		Name        string `json:"dev"`
		NumaNode    uint32 `json:"numa_node"`
		Size        uint64 `json:"size"`
	}

	// ScmNamespaces is a type alias for []ScmNamespace that implements fmt.Stringer.
	ScmNamespaces []ScmNamespace

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
		CtrlrPciAddr    string
	}

	// NvmeNamespace represents an individual NVMe namespace on a device.
	NvmeNamespace struct {
		ID           int32
		Size         int32
		CtrlrPciAddr string
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

func (m *ScmModule) String() string {
	return fmt.Sprintf("PhysicalID:%d Capacity:%s Location:(socket:%d memctrlr:%d "+
		"chan:%d pos:%d)", m.PhysicalID, bytesize.New(float64(m.Capacity)),
		m.SocketID, m.ControllerID, m.ChannelID, m.ChannelPosition)
}

func (ms ScmModules) String() string {
	var buf bytes.Buffer

	if len(ms) == 0 {
		return "\t\tnone\n"
	}

	for _, m := range ms {
		fmt.Fprintf(&buf, "\t\t%s\n", &m)
	}

	return buf.String()
}

// Summary reports accumulated storage space and the number of modules.
func (ms ScmModules) Summary() string {
	tCap := bytesize.New(0)
	for _, m := range ms {
		tCap += bytesize.New(float64(m.Capacity))
	}

	return fmt.Sprintf("%s total capacity over %d %s (unprepared)",
		tCap, len(ms), common.Pluralise("module", len(ms)))
}

func (n *ScmNamespace) String() string {
	return fmt.Sprintf("Device:%s Socket:%d Capacity:%s", n.BlockDevice, n.NumaNode,
		bytesize.New(float64(n.Size)))
}

func (ns ScmNamespaces) String() string {
	var buf bytes.Buffer

	if len(ns) == 0 {
		return "\t\tnone\n"
	}

	for _, n := range ns {
		fmt.Fprintf(&buf, "\t\t%s\n", &n)
	}

	return buf.String()
}

// Summary reports accumulated storage space and the number of namespaces.
func (ns ScmNamespaces) Summary() string {
	tCap := bytesize.New(0)
	for _, n := range ns {
		tCap += bytesize.New(float64(n.Size))
	}

	return fmt.Sprintf("%s total capacity over %d %s",
		tCap, len(ns), common.Pluralise("namespace", len(ns)))
}

// ctrlrDetail provides custom string representation for Controller type
// defined outside this package.
func (ncs NvmeControllers) ctrlrDetail(buf *bytes.Buffer, c *NvmeController) {
	if c == nil {
		fmt.Fprintf(buf, "<nil>\n")
		return
	}

	fmt.Fprintf(buf, "\t\tPCI Addr:%s Serial:%s Model:%s Fwrev:%s Socket:%d\n",
		c.PciAddr, c.Serial, c.Model, c.FwRev, c.SocketID)

	for _, ns := range c.Namespaces {
		fmt.Fprintf(buf, "\t\t\tNamespace: id:%d capacity:%s\n", ns.ID,
			bytesize.GB*bytesize.New(float64(ns.Size)))
	}
}

func (ncs NvmeControllers) String() string {
	buf := bytes.NewBufferString("NVMe controllers and namespaces:\n")

	if len(ncs) == 0 {
		fmt.Fprint(buf, "\t\tnone\n")
		return buf.String()
	}

	for _, ctrlr := range ncs {
		ncs.ctrlrDetail(buf, ctrlr)
	}

	return buf.String()
}
