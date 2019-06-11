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
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

// ScmMountResults is an alias for protobuf ScmMountResult message slice
// representing operation results on a number of SCM mounts.
type ScmMountResults []*pb.ScmMountResult

// ScmModuleResults is an alias for protobuf ScmModuleResult message slice
// representing operation results on a number of SCM modules.
type ScmModuleResults []*pb.ScmModuleResult

// NvmeControllerResults is an alias for protobuf NvmeControllerResult messages
// representing operation results on a number of NVMe controllers.
type NvmeControllerResults []*pb.NvmeControllerResult

// ScmModules is an alias for protobuf ScmModule message slice representing
// a number of SCM modules installed on a storage node.
type ScmModules []*pb.ScmModule

// ScmMounts is an alias for protobuf ScmMount message slice representing
// a number of mounted SCM regions on a storage node.
type ScmMounts []*pb.ScmMount

// NvmeControllers is an alias for protobuf NvmeController message slice
// representing a number of NVMe SSD controllers installed on a storage node.
type NvmeControllers []*pb.NvmeController

// CtrlrResults contains controllers and/or results of operations on controllers
// and an error signifying a problem in making the request.
type CtrlrResults struct {
	Ctrlrs    NvmeControllers
	Responses NvmeControllerResults
	Err       error
}

// ModuleResults contains scm modules and/or results of operations on modules
// and an error signifying a problem in making the request.
type ModuleResults struct {
	Modules   ScmModules
	Responses ScmModuleResults
	Err       error
}

// MountResults contains modules and/or results of operations on mounted SCM
// regions and an error signifying a problem in making the request.
type MountResults struct {
	Mounts    ScmMounts
	Responses ScmMountResults
	Err       error
}

// storageResult generic container for results of storage subsystems queries.
type storageResult struct {
	nvmeCtrlr CtrlrResults
	scmModule ModuleResults
	scmMount  MountResults
}

// ClientCtrlrMap is an alias for query results of NVMe controllers (and
// any residing namespaces) on connected servers keyed on address.
type ClientCtrlrMap map[string]CtrlrResults

// ClientModuleMap is an alias for query results of SCM modules installed
// on connected servers keyed on address.
type ClientModuleMap map[string]ModuleResults

// ClientMountMap is an alias for query results of SCM regions mounted
// on connected servers keyed on address.
type ClientMountMap map[string]MountResults
