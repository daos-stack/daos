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

package common

import (
	"bytes"
	"fmt"

	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

// NvmeControllers is an alias for protobuf NvmeController message slice
// representing a number of NVMe SSD controllers installed on a storage node.
type NvmeControllers []*pb.NvmeController

func (nc NvmeControllers) String() string {
	var buf bytes.Buffer

	for _, ctrlr := range nc {
		fmt.Fprintf(
			&buf, "\tPCI Address:%s Serial:%s\n\tModel:%s Fwrev:%s\n",
			ctrlr.Pciaddr, ctrlr.Serial, ctrlr.Model, ctrlr.Fwrev)

		for _, ns := range ctrlr.Namespaces {
			fmt.Fprintf(
				&buf, "\t\tNamespace: %+v\n", ns)
		}

		for _, hs := range ctrlr.Healthstats {
			fmt.Fprintf(
				&buf, "\tHealth Stats:\n\t\tTemperature:%dK(%dC)\n",
				hs.Temp, hs.Temp - 273)

			if hs.Tempwarn > 0 {
				fmt.Fprintf(&buf, "\t\t\tWarning Time:%d\n",
					uint64(hs.Tempwarn))
			}
			if hs.Tempcrit > 0 {
				fmt.Fprintf(&buf, "\t\t\tCritical Time:%d\n",
					uint64(hs.Tempcrit))
			}

			fmt.Fprintf(&buf, "\t\tController Busy Time:%d minutes\n",
				uint64(hs.Ctrlbusy))
			fmt.Fprintf(&buf, "\t\tPower Cycles:%d\n",
				uint64(hs.Powercycles))
			fmt.Fprintf(&buf, "\t\tPower On Hours:%d hours\n",
				uint64(hs.Poweronhours))
			fmt.Fprintf(&buf, "\t\tUnsafe Shutdowns:%d\n",
				uint64(hs.Unsafeshutdowns))
			fmt.Fprintf(&buf, "\t\tMedia Errors:%d\n",
				uint64(hs.Mediaerrors))
			fmt.Fprintf(&buf, "\t\tError Log Entries:%d\n",
				uint64(hs.Errorlogs))

			fmt.Fprintf(&buf, "\t\tCritical Warnings:\n")
			fmt.Fprintf(&buf, "\t\t\tTemperature: ")
			if hs.Tempwarning {
				fmt.Fprintf(&buf, "WARNING\n")
			} else {
				fmt.Fprintf(&buf, "OK\n");
			}
			fmt.Fprintf(&buf, "\t\t\tAvailable Spare: ")
			if hs.Availspare {
				fmt.Fprintf(&buf, "WARNING\n")
			} else {
				fmt.Fprintf(&buf, "OK\n")
			}
			fmt.Fprintf(&buf, "\t\t\tDevice Reliability: ")
			if hs.Reliability {
				fmt.Fprintf(&buf, "WARNING\n")
			} else {
				fmt.Fprintf(&buf, "OK\n");
			}
			fmt.Fprintf(&buf, "\t\t\tRead Only: ")
			if hs.Readonly {
				fmt.Fprintf(&buf, "WARNING\n")
			} else {
				fmt.Fprintf(&buf, "OK\n");
			}
			fmt.Fprintf(&buf, "\t\t\tVolatile Memory Backup: ")
			if hs.Volatilemem {
				fmt.Fprintf(&buf, "WARNING\n")
			} else {
				fmt.Fprintf(&buf, "OK\n");
			}
		}
	}

	return buf.String()
}

// NvmeNamespaces is an alias for protobuf NvmeController_Namespace message slice
// representing namespaces existing on a NVMe SSD.
type NvmeNamespaces []*pb.NvmeController_Namespace

type NvmeHealthstats []*pb.NvmeController_Health

// NvmeControllerResults is an alias for protobuf NvmeControllerResult messages
// representing operation results on a number of NVMe controllers.
type NvmeControllerResults []*pb.NvmeControllerResult

func (ncr NvmeControllerResults) String() string {
	var buf bytes.Buffer

	for _, resp := range ncr {
		fmt.Fprintf(
			&buf, "\tpci-address %s: status %s", resp.Pciaddr, resp.State.Status)

		if resp.State.Error != "" {
			fmt.Fprintf(&buf, " error: %s", resp.State.Error)
		}
		if resp.State.Info != "" {
			fmt.Fprintf(&buf, " info: %s", resp.State.Info)
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

// ScmMounts is an alias for protobuf ScmMount message slice representing
// a number of mounted SCM regions on a storage node.
type ScmMounts []*pb.ScmMount

func (sm ScmMounts) String() string {
	var buf bytes.Buffer

	for _, mount := range sm {
		fmt.Fprintf(&buf, "\t%+v\n", mount)
	}

	return buf.String()
}

// ScmMountResults is an alias for protobuf ScmMountResult message slice
// representing operation results on a number of SCM mounts.
type ScmMountResults []*pb.ScmMountResult

func (smr ScmMountResults) String() string {
	var buf bytes.Buffer

	for _, resp := range smr {
		fmt.Fprintf(
			&buf, "\tmntpoint %s: status %s", resp.Mntpoint, resp.State.Status)

		if resp.State.Error != "" {
			fmt.Fprintf(&buf, " error: %s", resp.State.Error)
		}
		if resp.State.Info != "" {
			fmt.Fprintf(&buf, " info: %s", resp.State.Info)
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
type ScmModules []*pb.ScmModule

func (sm ScmModules) String() string {
	var buf bytes.Buffer

	for _, module := range sm {
		fmt.Fprintf(&buf, "\t%+v\n", module)
	}

	return buf.String()
}

// ScmModuleResults is an alias for protobuf ScmModuleResult message slice
// representing operation results on a number of SCM modules.
type ScmModuleResults []*pb.ScmModuleResult

func (smr ScmModuleResults) String() string {
	var buf bytes.Buffer

	for _, resp := range smr {
		fmt.Fprintf(
			&buf, "\tmodule location %+v: status %s", resp.Loc, resp.State.Status)

		if resp.State.Error != "" {
			fmt.Fprintf(&buf, " error: %s", resp.State.Error)
		}
		if resp.State.Info != "" {
			fmt.Fprintf(&buf, " info: %s", resp.State.Info)
		}

		fmt.Fprintf(&buf, "\n")
	}

	return buf.String()
}

// ModuleResults contains scm modules and/or results of operations on modules
// and an error signifying a problem in making the request.
type ModuleResults struct {
	Modules   ScmModules
	Responses ScmModuleResults
	Err       error
}

func (mr ModuleResults) String() string {
	if mr.Err != nil {
		return mr.Err.Error()
	}
	if len(mr.Modules) > 0 {
		return mr.Modules.String()
	}
	if len(mr.Responses) > 0 {
		return mr.Responses.String()
	}

	return "no scm modules found"
}
