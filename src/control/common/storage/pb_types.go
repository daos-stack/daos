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

package common_storage

import (
	"bytes"
	"fmt"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	bytesize "github.com/inhies/go-bytesize"
)

// NvmeNamespaces is an alias for protobuf NvmeController_Namespace message slice
// representing namespaces existing on a NVMe SSD.
type NvmeNamespaces []*ctlpb.NvmeController_Namespace

// NvmeControllers is an alias for protobuf NvmeController message slice
// representing a number of NVMe SSD controllers installed on a storage node.
type NvmeControllers []*ctlpb.NvmeController

func (ncs NvmeControllers) healthDetail(buf *bytes.Buffer, c *ctlpb.NvmeController) {
	stat := c.Healthstats

	fmt.Fprintf(buf, "\tHealth Stats:\n\t\tTemperature:%dK(%dC)\n", stat.Temp, stat.Temp-273)

	if stat.Tempwarn > 0 {
		fmt.Fprintf(buf, "\t\t\tWarning Time:%d\n", uint64(stat.Tempwarn))
	}
	if stat.Tempcrit > 0 {
		fmt.Fprintf(buf, "\t\t\tCritical Time:%d\n", uint64(stat.Tempcrit))
	}

	fmt.Fprintf(buf, "\t\tController Busy Time:%d minutes\n", uint64(stat.Ctrlbusy))
	fmt.Fprintf(buf, "\t\tPower Cycles:%d\n", uint64(stat.Powercycles))
	fmt.Fprintf(buf, "\t\tPower On Hours:%d hours\n", uint64(stat.Poweronhours))
	fmt.Fprintf(buf, "\t\tUnsafe Shutdowns:%d\n", uint64(stat.Unsafeshutdowns))
	fmt.Fprintf(buf, "\t\tMedia Errors:%d\n", uint64(stat.Mediaerrors))
	fmt.Fprintf(buf, "\t\tError Log Entries:%d\n", uint64(stat.Errorlogs))

	fmt.Fprintf(buf, "\t\tCritical Warnings:\n")
	fmt.Fprintf(buf, "\t\t\tTemperature: ")
	if stat.Tempwarning {
		fmt.Fprintf(buf, "WARNING\n")
	} else {
		fmt.Fprintf(buf, "OK\n")
	}
	fmt.Fprintf(buf, "\t\t\tAvailable Spare: ")
	if stat.Availspare {
		fmt.Fprintf(buf, "WARNING\n")
	} else {
		fmt.Fprintf(buf, "OK\n")
	}
	fmt.Fprintf(buf, "\t\t\tDevice Reliability: ")
	if stat.Reliability {
		fmt.Fprintf(buf, "WARNING\n")
	} else {
		fmt.Fprintf(buf, "OK\n")
	}
	fmt.Fprintf(buf, "\t\t\tRead Only: ")
	if stat.Readonly {
		fmt.Fprintf(buf, "WARNING\n")
	} else {
		fmt.Fprintf(buf, "OK\n")
	}
	fmt.Fprintf(buf, "\t\t\tVolatile Memory Backup: ")
	if stat.Volatilemem {
		fmt.Fprintf(buf, "WARNING\n")
	} else {
		fmt.Fprintf(buf, "OK\n")
	}
}

// ctrlrDetail provides custom string representation for Controller type
// defined outside this package.
func (ncs NvmeControllers) ctrlrDetail(buf *bytes.Buffer, c *ctlpb.NvmeController) {
	fmt.Fprintf(buf, "\tPCI Addr:%s Serial:%s Model:%s Fwrev:%s Socket:%d\n",
		c.Pciaddr, c.Serial, c.Model, c.Fwrev, c.Socketid)

	for _, ns := range c.Namespaces {
		fmt.Fprintf(buf, "\t\tNamespace: id:%d capacity:%s\n", ns.Id, bytesize.GB*bytesize.New(float64(ns.Capacity)))
	}
}

func (ncs NvmeControllers) String() string {
	buf := bytes.NewBufferString("NVMe SSD controller and constituent namespaces:\n")

	for _, ctrlr := range ncs {
		ncs.ctrlrDetail(buf, ctrlr)
	}

	return buf.String()
}

// StringHealthStats returns full string representation including NVMe health
// statistics as well as controller and namespace details.
func (ncs NvmeControllers) StringHealthStats() string {
	buf := bytes.NewBufferString(
		"NVMe SSD controller, constituent namespaces and health statistics:\n")

	for _, ctrlr := range ncs {
		ncs.ctrlrDetail(buf, ctrlr)
		ncs.healthDetail(buf, ctrlr)
	}

	return buf.String()
}

// NvmeControllerResults is an alias for protobuf NvmeControllerResult messages
// representing operation results on a number of NVMe controllers.
type NvmeControllerResults []*ctlpb.NvmeControllerResult

func (ncr NvmeControllerResults) HasErrors() bool {
	for _, res := range ncr {
		if res.State.Error != "" {
			return true
		}
	}
	return false
}

func (ncr NvmeControllerResults) String() string {
	var buf bytes.Buffer

	for _, resp := range ncr {
		fmt.Fprintf(&buf, "\tPCI Addr:%s Status:%s", resp.Pciaddr, resp.State.Status)

		if resp.State.Error != "" {
			fmt.Fprintf(&buf, " Error:%s", resp.State.Error)
		}
		if resp.State.Info != "" {
			fmt.Fprintf(&buf, " Info:%s", resp.State.Info)
		}

		fmt.Fprintf(&buf, "\n")
	}

	return buf.String()
}

// CtrlrResults contains controllers and/or results of operations on controllers
// and an error signifying a problem in making the request.
type CtrlrResults struct {
	Ctrlrs    NvmeControllers
	Responses NvmeControllerResults
	Err       error
}

func (cr CtrlrResults) String() string {
	if cr.Err != nil {
		return cr.Err.Error()
	}
	if len(cr.Ctrlrs) > 0 {
		return cr.Ctrlrs.String()
	}
	if len(cr.Responses) > 0 {
		return cr.Responses.String()
	}

	return "no controllers found"
}

// ScmNamespaces is an alias for protobuf PmemDevice message slice representing
// a number of PMEM device files created on SCM namespaces on a storage node.
type ScmNamespaces []*ctlpb.PmemDevice

func (pds ScmNamespaces) String() string {
	var buf bytes.Buffer

	for _, pd := range pds {
		fmt.Fprintf(&buf, "\t%s\n", pd)
	}

	return buf.String()
}

// ScmMounts are protobuf representations of mounted SCM namespaces identified
// by mount points
type ScmMounts []*ctlpb.ScmMount

func (sm ScmMounts) String() string {
	var buf bytes.Buffer

	for _, mount := range sm {
		fmt.Fprintf(&buf, "\t%+v\n", mount)
	}

	return buf.String()
}

// ScmMountResults is an alias for protobuf ScmMountResult message slice
// representing operation results on a number of SCM mounts.
type ScmMountResults []*ctlpb.ScmMountResult

func (smr ScmMountResults) HasErrors() bool {
	for _, res := range smr {
		if res.State.Error != "" {
			return true
		}
	}
	return false
}

func (smr ScmMountResults) String() string {
	var buf bytes.Buffer

	for _, resp := range smr {
		fmt.Fprintf(
			&buf, "\tMntpoint:%s Status:%s", resp.Mntpoint, resp.State.Status)

		if resp.State.Error != "" {
			fmt.Fprintf(&buf, " Error:%s", resp.State.Error)
		}
		if resp.State.Info != "" {
			fmt.Fprintf(&buf, " Info:%s", resp.State.Info)
		}

		fmt.Fprintf(&buf, "\n")
	}

	return buf.String()
}

// MountResults contains modules and/or results of operations on mounted SCM
// regions and an error signifying a problem in making the request.
type MountResults struct {
	Mounts    ScmMounts
	Responses ScmMountResults
	Err       error
}

func (mr MountResults) String() string {
	if mr.Err != nil {
		return mr.Err.Error()
	}
	if len(mr.Mounts) > 0 {
		return mr.Mounts.String()
	}
	if len(mr.Responses) > 0 {
		return mr.Responses.String()
	}

	return "no scm mounts found"
}

// ScmModules is an alias for protobuf ScmModule message slice representing
// a number of SCM modules installed on a storage node.
type ScmModules []*ctlpb.ScmModule

// ScmModuleResults is an alias for protobuf ScmModuleResult message slice
// representing operation results on a number of SCM modules.
type ScmModuleResults []*ctlpb.ScmModuleResult

func (smr ScmModuleResults) String() string {
	var buf bytes.Buffer

	for _, resp := range smr {
		fmt.Fprintf(&buf,
			"\tModule Location:(socket:%d memctrlr:%d chan:%d "+
				"pos:%d) Status:%s",
			resp.Loc.Socket, resp.Loc.Memctrlr, resp.Loc.Channel,
			resp.Loc.Channelpos, resp.State.Status)

		if resp.State.Error != "" {
			fmt.Fprintf(&buf, " Error:%s", resp.State.Error)
		}
		if resp.State.Info != "" {
			fmt.Fprintf(&buf, " Info:%s", resp.State.Info)
		}

		fmt.Fprintf(&buf, "\n")
	}

	return buf.String()
}
