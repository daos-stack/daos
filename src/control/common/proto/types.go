//
// (C) Copyright 2019-2021 Intel Corporation.
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

// NvmeHealth is an alias for protobuf NvmeController_Health message.
type NvmeHealth ctlpb.NvmeController_Health

// FromNative converts storage package type to protobuf equivalent.
func (pb *NvmeHealth) FromNative(native *storage.NvmeHealth) error {
	return convert.Types(native, pb)
}

// ToNative converts pointer receiver alias type to storage package equivalent.
func (pb *NvmeHealth) ToNative() (*storage.NvmeHealth, error) {
	native := new(storage.NvmeHealth)
	return native, convert.Types(pb, native)
}

// AsProto converts pointer receiver alias type to protobuf type.
func (pb *NvmeHealth) AsProto() *ctlpb.NvmeController_Health {
	return (*ctlpb.NvmeController_Health)(pb)
}

// NvmeNamespace is an alias for protobuf NvmeController_Namespace message.
type NvmeNamespace ctlpb.NvmeController_Namespace

// FromNative converts storage package type to protobuf equivalent.
func (pb *NvmeNamespace) FromNative(native *storage.NvmeNamespace) error {
	return convert.Types(native, pb)
}

// ToNative converts pointer receiver alias type to storage package equivalent.
func (pb *NvmeNamespace) ToNative() (*storage.NvmeNamespace, error) {
	native := new(storage.NvmeNamespace)
	return native, convert.Types(pb, native)
}

// AsProto converts pointer receiver alias type to protobuf type.
func (pb *NvmeNamespace) AsProto() *ctlpb.NvmeController_Namespace {
	return (*ctlpb.NvmeController_Namespace)(pb)
}

// NvmeNamespaces is an alias for protobuf NvmeController_Namespace message slice
// representing namespaces existing on a NVMe SSD.
type NvmeNamespaces []*ctlpb.NvmeController_Namespace

// SmdDevice is an alias for protobuf NvmeController_SmdDevice message
// representing DAOS server meta data existing on a NVMe SSD.
type SmdDevice ctlpb.NvmeController_SmdDevice

// FromNative converts storage package type to protobuf equivalent.
func (pb *SmdDevice) FromNative(native *storage.SmdDevice) error {
	return convert.Types(native, pb)
}

// ToNative converts pointer receiver alias type to storage package equivalent.
func (pb *SmdDevice) ToNative() (*storage.SmdDevice, error) {
	native := new(storage.SmdDevice)
	return native, convert.Types(pb, native)
}

// AsProto converts pointer receiver alias type to protobuf type.
func (pb *SmdDevice) AsProto() *ctlpb.NvmeController_SmdDevice {
	return (*ctlpb.NvmeController_SmdDevice)(pb)
}

// SmdDevices is an alias for protobuf NvmeController_SmdDevice message slice
// representing DAOS server meta data existing on a NVMe SSD.
type SmdDevices []*ctlpb.NvmeController_SmdDevice

// NvmeController is an alias for protobuf NvmeController message slice.
type NvmeController ctlpb.NvmeController

// FromNative converts storage package type to protobuf equivalent.
func (pb *NvmeController) FromNative(native *storage.NvmeController) error {
	return convert.Types(native, pb)
}

// ToNative converts pointer receiver alias type to storage package equivalent.
func (pb *NvmeController) ToNative() (*storage.NvmeController, error) {
	native := new(storage.NvmeController)
	return native, convert.Types(pb, native)
}

// AsProto converts pointer receiver alias type to protobuf type.
func (pb *NvmeController) AsProto() *ctlpb.NvmeController {
	return (*ctlpb.NvmeController)(pb)
}

// NvmeControllers is an alias for protobuf NvmeController message slice
// representing a number of NVMe SSD controllers installed on a storage node.
type NvmeControllers []*ctlpb.NvmeController

// FromNative converts storage package type to protobuf equivalent.
func (pb *NvmeControllers) FromNative(native storage.NvmeControllers) error {
	return convert.Types(native, pb)
}

// ToNative converts pointer receiver alias type to storage package equivalent.
func (pb *NvmeControllers) ToNative() (storage.NvmeControllers, error) {
	native := make(storage.NvmeControllers, 0, len(*pb))
	return native, convert.Types(pb, &native)
}

// NvmeControllerResults is an alias for protobuf NvmeControllerResult messages
// representing operation results on a number of NVMe controllers.
type NvmeControllerResults []*ctlpb.NvmeControllerResult

// HasErrors indicates whether any controller result has non-successful status.
func (ncr NvmeControllerResults) HasErrors() bool {
	for _, res := range ncr {
		if res.State.Status != ctlpb.ResponseStatus_CTL_SUCCESS {
			return true
		}
	}
	return false
}

// ScmModule is an alias for protobuf ScmModule message representing an SCM
// persistent memory module installed on a storage node.
type ScmModule ctlpb.ScmModule

// FromNative converts storage package type to protobuf equivalent.
func (pb *ScmModule) FromNative(native *storage.ScmModule) error {
	return convert.Types(native, pb)
}

// ToNative converts pointer receiver alias type to storage package equivalent.
func (pb *ScmModule) ToNative() (*storage.ScmModule, error) {
	native := new(storage.ScmModule)
	return native, convert.Types(pb, native)
}

// AsProto converts pointer receiver alias type to protobuf type.
func (pb *ScmModule) AsProto() *ctlpb.ScmModule {
	return (*ctlpb.ScmModule)(pb)
}

// ScmModules is an alias for protobuf ScmModule message slice representing
// a number of SCM modules installed on a storage node.
type ScmModules []*ctlpb.ScmModule

// FromNative converts storage package type to protobuf equivalent.
func (pb *ScmModules) FromNative(native storage.ScmModules) error {
	return convert.Types(native, pb)
}

// ToNative converts pointer receiver alias type to storage package equivalent.
func (pb *ScmModules) ToNative() (storage.ScmModules, error) {
	native := make(storage.ScmModules, 0, len(*pb))
	return native, convert.Types(pb, &native)
}

// ScmModuleResults is an alias for protobuf ScmModuleResult message slice
// representing operation results on a number of SCM modules.
type ScmModuleResults []*ctlpb.ScmModuleResult

// ScmNamespace is an alias for protobuf ScmNamespace message representing a
// pmem block device created on an appdirect set of persistent memory modules.
type ScmNamespace ctlpb.ScmNamespace

// FromNative converts storage package type to protobuf equivalent.
func (pb *ScmNamespace) FromNative(native *storage.ScmNamespace) error {
	return convert.Types(native, pb)
}

// ToNative converts pointer receiver alias type to storage package equivalent.
func (pb *ScmNamespace) ToNative() (*storage.ScmNamespace, error) {
	native := new(storage.ScmNamespace)
	return native, convert.Types(pb, native)
}

// AsProto converts pointer receiver alias type to protobuf type.
func (pb *ScmNamespace) AsProto() *ctlpb.ScmNamespace {
	return (*ctlpb.ScmNamespace)(pb)
}

// ScmNamespaces is an alias for protobuf ScmNamespace message slice representing
// a number of SCM modules installed on a storage node.
type ScmNamespaces []*ctlpb.ScmNamespace

// FromNative converts storage package type to protobuf equivalent.
func (pb *ScmNamespaces) FromNative(native storage.ScmNamespaces) error {
	return convert.Types(native, pb)
}

// ToNative converts pointer receiver alias type to storage package equivalent.
func (pb *ScmNamespaces) ToNative() (storage.ScmNamespaces, error) {
	native := make(storage.ScmNamespaces, 0, len(*pb))
	return native, convert.Types(pb, &native)
}

// ScmMountPoint is an alias for protobuf ScmNamespace_Mount message representing
// the OS mount point target at which a pmem block device is mounted.
type ScmMountPoint ctlpb.ScmNamespace_Mount

// FromNative converts storage package type to protobuf equivalent.
func (pb *ScmMountPoint) FromNative(native *storage.ScmMountPoint) error {
	return convert.Types(native, pb)
}

// ToNative converts pointer receiver alias type to storage package equivalent.
func (pb *ScmMountPoint) ToNative() (*storage.ScmMountPoint, error) {
	native := new(storage.ScmMountPoint)
	return native, convert.Types(pb, native)
}

// AsProto converts pointer receiver alias type to protobuf type.
func (pb *ScmMountPoint) AsProto() *ctlpb.ScmNamespace_Mount {
	return (*ctlpb.ScmNamespace_Mount)(pb)
}

// ScmMountResults is an alias for protobuf ScmMountResult message slice
// representing operation results on a number of SCM mounts.
type ScmMountResults []*ctlpb.ScmMountResult

// HasErrors indicates whether any mount result has non-successful status.
func (smr ScmMountResults) HasErrors() bool {
	for _, res := range smr {
		if res.State.Status != ctlpb.ResponseStatus_CTL_SUCCESS {
			return true
		}
	}
	return false
}

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
		svcReps := make([]uint32, 0, len(pbPool.SvcReps))
		svcReps = append(svcReps, pbPool.SvcReps...)

		pools = append(pools, &common.PoolDiscovery{
			UUID:        pbPool.Uuid,
			SvcReplicas: svcReps,
		})
	}

	return pools
}
