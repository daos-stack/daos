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
package scm_types

import (
	"bytes"
	"fmt"
)

//go:generate stringer -type=ScmState
type ScmState int

const (
	ScmStateUnknown ScmState = iota
	ScmStateNoRegions
	ScmStateFreeCapacity
	ScmStateNoCapacity
)

type (
	// Module represents a SCM DIMM.
	//
	// This is a simplified representation of the raw struct used in the ipmctl package.
	Module struct {
		ChannelID       uint32
		ChannelPosition uint32
		ControllerID    uint32
		SocketID        uint32
		PhysicalID      uint32
		Capacity        uint64
	}

	Modules []Module

	// Namespace represents a mapping between AppDirect regions and block device files.
	Namespace struct {
		UUID        string `json:"uuid"`
		BlockDevice string `json:"blockdev"`
		Name        string `json:"dev"`
		NumaNode    uint32 `json:"numa_node"`
	}

	Namespaces []Namespace

	// ScanRequest defines the parameters for a Scan operation.
	ScanRequest struct {
		Rescan bool
	}

	// ScanResponse contains information gleaned during a successful Scan operation.
	ScanResponse struct {
		State      ScmState
		Modules    Modules
		Namespaces Namespaces
	}
)

func (m *Module) String() string {
	return fmt.Sprintf("PhysicalID:%d Capacity:%d Location:(socket:%d memctrlr:%d "+
		"chan:%d pos:%d)", m.PhysicalID, m.Capacity, m.SocketID, m.ControllerID,
		m.ChannelID, m.ChannelPosition)
}

func (ms *Modules) String() string {
	var buf bytes.Buffer

	for _, m := range *ms {
		fmt.Fprintf(&buf, "\t%s\n", &m)
	}

	return buf.String()
}

func (n *Namespace) String() string {
	return fmt.Sprintf("%s/%s/%s (NUMA %d)", n.Name, n.BlockDevice, n.UUID, n.NumaNode)
}

func (ns *Namespaces) String() string {
	var buf bytes.Buffer

	for _, n := range *ns {
		fmt.Fprintf(&buf, "\t%s\n", &n)
	}

	return buf.String()
}

func (sr *ScanResponse) String() string {
	var buf bytes.Buffer

	// Zero uninitialised value is Unknown (0)
	if sr.State != ScmStateUnknown {
		fmt.Fprintf(&buf, "SCM State: %s\n", sr.State.String())
	}

	if len(sr.Namespaces) > 0 {
		fmt.Fprintf(&buf, "SCM Namespaces:\n%s\n", &sr.Namespaces)
	} else {
		fmt.Fprintf(&buf, "SCM Modules:\n%s\n", &sr.Modules)
	}

	return buf.String()
}
