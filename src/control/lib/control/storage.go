//
// (C) Copyright 2020 Intel Corporation.
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

package control

import (
	"context"
	"sort"

	"github.com/golang/protobuf/proto"
	"github.com/mitchellh/hashstructure"
	"github.com/pkg/errors"
	"google.golang.org/grpc"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// HostStorage describes a host storage configuration which
// may apply to one or more hosts.
type HostStorage struct {
	// NvmeDevices contains the set of NVMe controllers (SSDs)
	// in this configuration.
	NvmeDevices storage.NvmeControllers `hash:"set" json:"nvme_devices"`

	// ScmModules contains the set of SCM modules (persistent
	// memory DIMMs) in this configuration.
	ScmModules storage.ScmModules `hash:"set" json:"scm_modules"`

	// ScmNamespaces contains the set of prepared SCM namespaces
	// (block devices) in this configuration.
	ScmNamespaces storage.ScmNamespaces `hash:"set" json:"scm_namespaces"`

	// ScmMountPoints contains the set of SCM mountpoints in
	// this configuration.
	ScmMountPoints storage.ScmMountPoints `hash:"set" json:"scm_mount_points"`

	// SmdInfo contains information obtained by querying the
	// host's metadata table, if available.
	SmdInfo *SmdInfo `json:"smd_info"`

	// RebootRequired indicates that a host reboot is necessary in order
	// to achieve some goal (SCM prep, etc.)
	RebootRequired bool `json:"reboot_required"`
}

// HashKey returns a uint64 value suitable for use as a key into
// a map of HostStorage configurations.
func (hs *HostStorage) HashKey() (uint64, error) {
	return hashstructure.Hash(hs, nil)
}

// HostStorageSet contains a HostStorage configuration and the
// set of hosts matching this configuration.
type HostStorageSet struct {
	HostStorage *HostStorage      `json:"storage"`
	HostSet     *hostlist.HostSet `json:"hosts"`
}

// NewHostStorageSet returns an initialized HostStorageSet for the given
// host address and HostStorage configuration.
func NewHostStorageSet(hostAddr string, hs *HostStorage) (*HostStorageSet, error) {
	hostSet, err := hostlist.CreateSet(hostAddr)
	if err != nil {
		return nil, err
	}
	return &HostStorageSet{
		HostStorage: hs,
		HostSet:     hostSet,
	}, nil
}

// HostStorageMap provides a map of HostStorage keys to HostStorageSet values.
type HostStorageMap map[uint64]*HostStorageSet

// Add inserts the given host address to a matching HostStorageSet or
// creates a new one.
func (hsm HostStorageMap) Add(hostAddr string, hs *HostStorage) (err error) {
	hk, err := hs.HashKey()
	if err != nil {
		return err
	}
	if _, exists := hsm[hk]; !exists {
		hsm[hk], err = NewHostStorageSet(hostAddr, hs)
		return
	}
	_, err = hsm[hk].HostSet.Insert(hostAddr)
	return
}

// Keys returns a set of storage map keys sorted by hosts.
func (hsm HostStorageMap) Keys() []uint64 {
	sets := make([]string, 0, len(hsm))
	keys := make([]uint64, len(hsm))
	setToKeys := make(map[string]uint64)
	for key, hss := range hsm {
		rs := hss.HostSet.RangedString()
		sets = append(sets, rs)
		setToKeys[rs] = key
	}
	sort.Strings(sets)
	for i, set := range sets {
		keys[i] = setToKeys[set]
	}
	return keys
}

type (
	// StorageScanReq contains the parameters for a storage scan request.
	StorageScanReq struct {
		unaryRequest
		ConfigDevicesOnly bool
	}

	// StorageScanResp contains the response from a storage scan request.
	StorageScanResp struct {
		HostErrorsResp
		HostStorage HostStorageMap
	}
)

// addHostResponse is responsible for validating the given HostResponse
// and adding it to the StorageScanResp.
func (ssp *StorageScanResp) addHostResponse(hr *HostResponse) (err error) {
	pbResp, ok := hr.Message.(*ctlpb.StorageScanResp)
	if !ok {
		return errors.Errorf("unable to unpack message: %+v", hr.Message)
	}

	hs := new(HostStorage)
	switch pbResp.GetNvme().GetState().GetStatus() {
	case ctlpb.ResponseStatus_CTL_SUCCESS:
		if err := convert.Types(pbResp.GetNvme().GetCtrlrs(), &hs.NvmeDevices); err != nil {
			return ssp.addHostError(hr.Addr, err)
		}
	default:
		if pbErr := pbResp.GetNvme().GetState().GetError(); pbErr != "" {
			if err := ssp.addHostError(hr.Addr, errors.New(pbErr)); err != nil {
				return err
			}
		}
	}

	switch pbResp.GetScm().GetState().GetStatus() {
	case ctlpb.ResponseStatus_CTL_SUCCESS:
		if err := convert.Types(pbResp.GetScm().GetModules(), &hs.ScmModules); err != nil {
			return ssp.addHostError(hr.Addr, err)
		}
		if err := convert.Types(pbResp.GetScm().GetNamespaces(), &hs.ScmNamespaces); err != nil {
			return ssp.addHostError(hr.Addr, err)
		}
	default:
		if pbErr := pbResp.GetScm().GetState().GetError(); pbErr != "" {
			if err := ssp.addHostError(hr.Addr, errors.New(pbErr)); err != nil {
				return err
			}
		}
	}

	if ssp.HostStorage == nil {
		ssp.HostStorage = make(HostStorageMap)
	}
	if err := ssp.HostStorage.Add(hr.Addr, hs); err != nil {
		return err
	}

	return
}

// StorageScan concurrently performs storage scans across all hosts
// supplied in the request's hostlist, or all configured hosts if not
// explicitly specified. The function blocks until all results (successful
// or otherwise) are received, and returns a single response structure
// containing results for all host scan operations.
func StorageScan(ctx context.Context, rpcClient UnaryInvoker, req *StorageScanReq) (*StorageScanResp, error) {
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return ctlpb.NewMgmtCtlClient(conn).StorageScan(ctx, &ctlpb.StorageScanReq{
			ConfigDevicesOnly: req.ConfigDevicesOnly,
		})
	})

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	ssr := new(StorageScanResp)
	for _, hostResp := range ur.Responses {
		if hostResp.Error != nil {
			if err := ssr.addHostError(hostResp.Addr, hostResp.Error); err != nil {
				return nil, err
			}
			continue
		}

		if err := ssr.addHostResponse(hostResp); err != nil {
			return nil, err
		}
	}

	return ssr, nil
}

type (
	// NvmePrepareReq contains the parameters for a NVMe prepare request.
	NvmePrepareReq struct {
		PCIWhiteList string
		NrHugePages  int32
		TargetUser   string
		Reset        bool
	}

	// ScmPrepareReq contains the parameters for a SCM prepare request.
	ScmPrepareReq struct {
		Reset bool
	}

	// StoragePrepareReq contains the parameters for a storage prepare request.
	StoragePrepareReq struct {
		unaryRequest
		NVMe *NvmePrepareReq
		SCM  *ScmPrepareReq
	}

	// StoragePrepareResp contains the response from a storage prepare request.
	StoragePrepareResp struct {
		HostErrorsResp
		HostStorage HostStorageMap
	}
)

// addHostResponse is responsible for validating the given HostResponse
// and adding it to the StoragePrepareResp.
func (ssp *StoragePrepareResp) addHostResponse(hr *HostResponse) (err error) {
	pbResp, ok := hr.Message.(*ctlpb.StoragePrepareResp)
	if !ok {
		return errors.Errorf("unable to unpack message: %+v", hr.Message)
	}

	hs := new(HostStorage)
	if pbResp.GetNvme().GetState().GetStatus() != ctlpb.ResponseStatus_CTL_SUCCESS {
		if pbErr := pbResp.GetNvme().GetState().GetError(); pbErr != "" {
			if err := ssp.addHostError(hr.Addr, errors.New(pbErr)); err != nil {
				return err
			}
		}
	}

	switch pbResp.GetScm().GetState().GetStatus() {
	case ctlpb.ResponseStatus_CTL_SUCCESS:
		if err := convert.Types(pbResp.GetScm().GetNamespaces(), &hs.ScmNamespaces); err != nil {
			return ssp.addHostError(hr.Addr, err)
		}
		hs.RebootRequired = pbResp.GetScm().GetRebootrequired()
	default:
		if pbErr := pbResp.GetScm().GetState().GetError(); pbErr != "" {
			if err := ssp.addHostError(hr.Addr, errors.New(pbErr)); err != nil {
				return err
			}
		}
	}

	if ssp.HostStorage == nil {
		ssp.HostStorage = make(HostStorageMap)
	}
	return ssp.HostStorage.Add(hr.Addr, hs)
}

// StoragePrepare concurrently performs storage preparation steps across
// all hosts supplied in the request's hostlist, or all configured hosts
// if not explicitly specified. The function blocks until all results
// (successful or otherwise) are received, and returns a single response
// structure containing results for all host storage prepare operations.
func StoragePrepare(ctx context.Context, rpcClient UnaryInvoker, req *StoragePrepareReq) (*StoragePrepareResp, error) {
	pbReq := new(ctlpb.StoragePrepareReq)
	if err := convert.Types(req, pbReq); err != nil {
		return nil, err
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return ctlpb.NewMgmtCtlClient(conn).StoragePrepare(ctx, pbReq)
	})

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	spr := new(StoragePrepareResp)
	for _, hostResp := range ur.Responses {
		if hostResp.Error != nil {
			if err := spr.addHostError(hostResp.Addr, hostResp.Error); err != nil {
				return nil, err
			}
			continue
		}

		if err := spr.addHostResponse(hostResp); err != nil {
			return nil, err
		}
	}

	return spr, nil
}

type (
	// StorageFormatReq contains the parameters for a storage format request.
	StorageFormatReq struct {
		unaryRequest
		Reformat bool
	}

	// StorageFormatResp contains the response from a storage format request.
	StorageFormatResp struct {
		HostErrorsResp
		HostStorage HostStorageMap
	}
)

// addHostResponse is responsible for validating the given HostResponse
// and adding it to the StorageFormatResp.
func (ssp *StorageFormatResp) addHostResponse(hr *HostResponse) (err error) {
	pbResp, ok := hr.Message.(*ctlpb.StorageFormatResp)
	if !ok {
		return errors.Errorf("unable to unpack message: %+v", hr.Message)
	}

	hs := new(HostStorage)
	for _, nvmeFmtResult := range pbResp.GetCrets() {
		switch nvmeFmtResult.GetState().GetStatus() {
		case ctlpb.ResponseStatus_CTL_SUCCESS:
			// If we didn't receive a PCI Address in the response,
			// then the device wasn't formatted.
			if nvmeFmtResult.GetPciaddr() == "" {
				continue
			}

			info := nvmeFmtResult.GetState().GetInfo()
			if info == "" {
				info = ctlpb.ResponseStatus_CTL_SUCCESS.String()
			}
			hs.NvmeDevices = append(hs.NvmeDevices, &storage.NvmeController{
				Info:    info,
				PciAddr: nvmeFmtResult.GetPciaddr(),
			})
		default:
			if err := ctlStateToErr(nvmeFmtResult.GetState()); err != nil {
				if err := ssp.addHostError(hr.Addr, err); err != nil {
					return err
				}
			}
		}
	}

	for _, scmFmtResult := range pbResp.GetMrets() {
		switch scmFmtResult.GetState().GetStatus() {
		case ctlpb.ResponseStatus_CTL_SUCCESS:
			info := scmFmtResult.GetState().GetInfo()
			if info == "" {
				info = ctlpb.ResponseStatus_CTL_SUCCESS.String()
			}
			hs.ScmMountPoints = append(hs.ScmMountPoints, &storage.ScmMountPoint{
				Info: info,
				Path: scmFmtResult.GetMntpoint(),
			})
		default:
			if err := ctlStateToErr(scmFmtResult.GetState()); err != nil {
				if err := ssp.addHostError(hr.Addr, err); err != nil {
					return err
				}
			}
		}
	}

	if ssp.HostStorage == nil {
		ssp.HostStorage = make(HostStorageMap)
	}
	return ssp.HostStorage.Add(hr.Addr, hs)
}

// StorageFormat concurrently performs storage preparation steps across
// all hosts supplied in the request's hostlist, or all configured hosts
// if not explicitly specified. The function blocks until all results
// (successful or otherwise) are received, and returns a single response
// structure containing results for all host storage prepare operations.
func StorageFormat(ctx context.Context, rpcClient UnaryInvoker, req *StorageFormatReq) (*StorageFormatResp, error) {
	pbReq := new(ctlpb.StorageFormatReq)
	if err := convert.Types(req, pbReq); err != nil {
		return nil, err
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return ctlpb.NewMgmtCtlClient(conn).StorageFormat(ctx, pbReq)
	})

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	spr := new(StorageFormatResp)
	for _, hostResp := range ur.Responses {
		if hostResp.Error != nil {
			if err := spr.addHostError(hostResp.Addr, hostResp.Error); err != nil {
				return nil, err
			}
			continue
		}

		if err := spr.addHostResponse(hostResp); err != nil {
			return nil, err
		}
	}

	return spr, nil
}
