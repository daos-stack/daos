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

package client

import (
	"fmt"
	"strings"

	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

// NvmeControllers is an alias for protobuf NvmeController message slice
// representing a number of NVMe SSD controllers installed on a storage node.
type NvmeControllers []*pb.NvmeController

func (nc NvmeControllers) String() string {
	var sb strings.Builder

	for _, ctrlr := range nc {
		sb.WriteString(fmt.Sprintf("%+v\n", ctrlr))
	}

	return sb.String()
}

// NvmeControllerResults is an alias for protobuf NvmeControllerResult messages
// representing operation results on a number of NVMe controllers.
type NvmeControllerResults []*pb.NvmeControllerResult

func (ncr NvmeControllerResults) String() string {
	var sb strings.Builder

	for _, resp := range ncr {
		sb.WriteString(
			fmt.Sprintf(
				"pci-address %s: status %s", resp.Pciaddr, resp.State.Status))

		if resp.State.Error != "" {
			sb.WriteString(fmt.Sprintf(" error: %s", resp.State.Error))
		}
		if resp.State.Info != "" {
			sb.WriteString(fmt.Sprintf(" info: %s", resp.State.Info))
		}

		sb.WriteString("\n")
	}

	return sb.String()
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

	return "unexpected error: no responses"
}

// ClientCtrlrMap is an alias for query results of NVMe controllers (and
// any residing namespaces) on connected servers keyed on address.
type ClientCtrlrMap map[string]CtrlrResults

func (ccm ClientCtrlrMap) String() string {
	var sb strings.Builder

	for server, result := range ccm {
		sb.WriteString(fmt.Sprintf("%s: %s\n", server, result))
	}

	return sb.String()
}

// ScmMounts is an alias for protobuf ScmMount message slice representing
// a number of mounted SCM regions on a storage node.
type ScmMounts []*pb.ScmMount

func (sm ScmMounts) String() string {
	var sb strings.Builder

	for _, mount := range sm {
		sb.WriteString(fmt.Sprintf("%+v\n", mount))
	}

	return sb.String()
}

// ScmMountResults is an alias for protobuf ScmMountResult message slice
// representing operation results on a number of SCM mounts.
type ScmMountResults []*pb.ScmMountResult

func (smr ScmMountResults) String() string {
	var sb strings.Builder

	for _, resp := range smr {
		sb.WriteString(
			fmt.Sprintf(
				"mntpoint %s: status %s", resp.Mntpoint, resp.State.Status))

		if resp.State.Error != "" {
			sb.WriteString(fmt.Sprintf(" error: %s", resp.State.Error))
		}
		if resp.State.Info != "" {
			sb.WriteString(fmt.Sprintf(" info: %s", resp.State.Info))
		}

		sb.WriteString("\n")
	}

	return sb.String()
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

	return "unexpected error: no responses"
}

// ClientMountMap is an alias for query results of SCM regions mounted
// on connected servers keyed on address.
type ClientMountMap map[string]MountResults

func (cmm ClientMountMap) String() string {
	var sb strings.Builder

	for server, result := range cmm {
		sb.WriteString(fmt.Sprintf("%s: %s\n", server, result))
	}

	return sb.String()
}

// ScmModules is an alias for protobuf ScmModule message slice representing
// a number of SCM modules installed on a storage node.
type ScmModules []*pb.ScmModule

func (sm ScmModules) String() string {
	var sb strings.Builder

	for _, module := range sm {
		sb.WriteString(fmt.Sprintf("%+v\n", module))
	}

	return sb.String()
}

// ScmModuleResults is an alias for protobuf ScmModuleResult message slice
// representing operation results on a number of SCM modules.
type ScmModuleResults []*pb.ScmModuleResult

func (smr ScmModuleResults) String() string {
	var sb strings.Builder

	for _, resp := range smr {
		sb.WriteString(
			fmt.Sprintf(
				"module location %+v: status %s", resp.Loc, resp.State.Status))

		if resp.State.Error != "" {
			sb.WriteString(fmt.Sprintf(" error: %s", resp.State.Error))
		}
		if resp.State.Info != "" {
			sb.WriteString(fmt.Sprintf(" info: %s", resp.State.Info))
		}

		sb.WriteString("\n")
	}

	return sb.String()
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

	return "unexpected error: no responses"
}

// ClientModuleMap is an alias for query results of SCM modules installed
// on connected servers keyed on address.
type ClientModuleMap map[string]ModuleResults

func (cmm ClientModuleMap) String() string {
	var sb strings.Builder

	for server, result := range cmm {
		sb.WriteString(fmt.Sprintf("%s: %s\n", server, result))
	}

	return sb.String()
}

// storageResult generic container for results of storage subsystems queries.
type storageResult struct {
	nvmeCtrlr CtrlrResults
	scmModule ModuleResults
	scmMount  MountResults
}
