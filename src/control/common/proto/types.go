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

package proto

import (
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/server/storage"
)

type NvmeControllerHealth ctlpb.NvmeController_Health

func (pb *NvmeControllerHealth) FromNative(native *storage.NvmeControllerHealth) error {
	return convert.Types(native, pb)
}

func (pb *NvmeControllerHealth) ToNative() (*storage.NvmeControllerHealth, error) {
	native := new(storage.NvmeControllerHealth)
	return native, convert.Types(pb, native)
}

func (pb *NvmeControllerHealth) AsProto() *ctlpb.NvmeController_Health {
	return (*ctlpb.NvmeController_Health)(pb)
}

type NvmeNamespace ctlpb.NvmeController_Namespace

func (pb *NvmeNamespace) FromNative(native *storage.NvmeNamespace) error {
	return convert.Types(native, pb)
}

func (pb *NvmeNamespace) ToNative() (*storage.NvmeNamespace, error) {
	native := new(storage.NvmeNamespace)
	return native, convert.Types(pb, native)
}

func (pb *NvmeNamespace) AsProto() *ctlpb.NvmeController_Namespace {
	return (*ctlpb.NvmeController_Namespace)(pb)
}

// NvmeNamespaces is an alias for protobuf NvmeController_Namespace message slice
// representing namespaces existing on a NVMe SSD.
type NvmeNamespaces []*ctlpb.NvmeController_Namespace

type NvmeController ctlpb.NvmeController

func (pb *NvmeController) FromNative(native *storage.NvmeController) error {
	return convert.Types(native, pb)
}

func (pb *NvmeController) ToNative() (*storage.NvmeController, error) {
	native := new(storage.NvmeController)
	return native, convert.Types(pb, native)
}

func (pb *NvmeController) AsProto() *ctlpb.NvmeController {
	return (*ctlpb.NvmeController)(pb)
}

// NvmeControllers is an alias for protobuf NvmeController message slice
// representing a number of NVMe SSD controllers installed on a storage node.
type NvmeControllers []*ctlpb.NvmeController

func (pb *NvmeControllers) FromNative(native storage.NvmeControllers) error {
	return convert.Types(native, pb)
}

func (pb *NvmeControllers) ToNative() (storage.NvmeControllers, error) {
	native := make(storage.NvmeControllers, 0, len(*pb))
	return native, convert.Types(pb, &native)
}

// NvmeControllerResults is an alias for protobuf NvmeControllerResult messages
// representing operation results on a number of NVMe controllers.
type NvmeControllerResults []*ctlpb.NvmeControllerResult

func (ncr NvmeControllerResults) HasErrors() bool {
	for _, res := range ncr {
		if res.State.Status != ctlpb.ResponseStatus_CTL_SUCCESS {
			return true
		}
	}
	return false
}

type ScmModule ctlpb.ScmModule

func (pb *ScmModule) FromNative(native *storage.ScmModule) error {
	return convert.Types(native, pb)
}

func (pb *ScmModule) ToNative() (*storage.ScmModule, error) {
	native := new(storage.ScmModule)
	return native, convert.Types(pb, native)
}

func (pb *ScmModule) AsProto() *ctlpb.ScmModule {
	return (*ctlpb.ScmModule)(pb)
}

// ScmModules is an alias for protobuf ScmModule message slice representing
// a number of SCM modules installed on a storage node.
type ScmModules []*ctlpb.ScmModule

func (pb *ScmModules) FromNative(native storage.ScmModules) error {
	return convert.Types(native, pb)
}

func (pb *ScmModules) ToNative() (storage.ScmModules, error) {
	native := make(storage.ScmModules, 0, len(*pb))
	return native, convert.Types(pb, &native)
}

type ScmNamespace ctlpb.ScmNamespace

func (pb *ScmNamespace) FromNative(native *storage.ScmNamespace) error {
	return convert.Types(native, pb)
}

func (pb *ScmNamespace) ToNative() (*storage.ScmNamespace, error) {
	native := new(storage.ScmNamespace)
	return native, convert.Types(pb, native)
}

func (pb *ScmNamespace) AsProto() *ctlpb.ScmNamespace {
	return (*ctlpb.ScmNamespace)(pb)
}

// ScmNamespaces is an alias for protobuf ScmNamespace message slice representing
// a number of SCM modules installed on a storage node.
type ScmNamespaces []*ctlpb.ScmNamespace

func (pb *ScmNamespaces) FromNative(native storage.ScmNamespaces) error {
	return convert.Types(native, pb)
}

func (pb *ScmNamespaces) ToNative() (storage.ScmNamespaces, error) {
	native := make(storage.ScmNamespaces, 0, len(*pb))
	return native, convert.Types(pb, &native)
}

// ScmMounts are protobuf representations of mounted SCM namespaces identified
// by mount points
type ScmMounts []*ctlpb.ScmMount

// ScmMountResults is an alias for protobuf ScmMountResult message slice
// representing operation results on a number of SCM mounts.
type ScmMountResults []*ctlpb.ScmMountResult

func (smr ScmMountResults) HasErrors() bool {
	for _, res := range smr {
		if res.State.Status != ctlpb.ResponseStatus_CTL_SUCCESS {
			return true
		}
	}
	return false
}

// ScmModuleResults is an alias for protobuf ScmModuleResult message slice
// representing operation results on a number of SCM modules.
type ScmModuleResults []*ctlpb.ScmModuleResult

// AccessControlListFromPB converts from the protobuf ACLResp structure to an
// AccessControlList structure.
func AccessControlListFromPB(pbACL *mgmtpb.ACLResp) *common.AccessControlList {
	if pbACL == nil {
		return &common.AccessControlList{}
	}
	return &common.AccessControlList{
		Entries:    pbACL.ACL,
		Owner:      pbACL.OwnerUser,
		OwnerGroup: pbACL.OwnerGroup,
	}
}

// PoolDiscoveriesFromPB converts the protobuf ListPoolsResp_Pool structures to
// PoolDiscovery structures.
func PoolDiscoveriesFromPB(pbPools []*mgmtpb.ListPoolsResp_Pool) []*common.PoolDiscovery {
	pools := make([]*common.PoolDiscovery, 0, len(pbPools))
	for _, pbPool := range pbPools {
		svcReps := make([]uint32, 0, len(pbPool.Svcreps))
		for _, rep := range pbPool.Svcreps {
			svcReps = append(svcReps, rep)
		}

		pools = append(pools, &common.PoolDiscovery{
			UUID:        pbPool.Uuid,
			SvcReplicas: svcReps,
		})
	}

	return pools
}
