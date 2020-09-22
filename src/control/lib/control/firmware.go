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
// +build firmware

package control

import (
	"context"
	"fmt"
	"sort"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"
	"google.golang.org/grpc"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/server/storage"
)

type (
	// FirmwareQueryReq is a request for firmware information for storage
	// devices.
	FirmwareQueryReq struct {
		unaryRequest
		SCM         bool     // Query SCM devices
		NVMe        bool     // Query NVMe devices
		Devices     []string // Specific devices to query
		ModelID     string   // Filter by model ID
		FirmwareRev string   // Filter by current FW revision
	}

	// FirmwareQueryResp returns storage device firmware information.
	FirmwareQueryResp struct {
		HostErrorsResp
		HostSCMFirmware  HostSCMQueryMap
		HostNVMeFirmware HostNVMeQueryMap
	}

	// HostSCMQueryMap maps a host name to a slice of SCM firmware query results.
	HostSCMQueryMap map[string][]*SCMQueryResult

	// SCMFirmwareResult represents the results of a firmware query
	// for a single SCM device.
	SCMQueryResult struct {
		Module storage.ScmModule
		Info   *storage.ScmFirmwareInfo
		Error  error
	}

	// HostNVMeQueryMap maps a host name to a slice of NVMe firmware query results.
	HostNVMeQueryMap map[string][]*NVMeQueryResult

	// NVMeQueryResult represents the results of a firmware query for a
	// single NVMe device.
	NVMeQueryResult struct {
		Device storage.NvmeController
	}
)

// Keys returns the sorted list of keys from the HostSCMQueryMap.
func (m HostSCMQueryMap) Keys() []string {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return keys
}

// Keys returns the sorted list of keys from the HostNVMeQueryMap.
func (m HostNVMeQueryMap) Keys() []string {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return keys
}

// addHostResponse is responsible for validating the given HostResponse
// and adding it to the FirmwareQueryResp.
func (qr *FirmwareQueryResp) addHostResponse(hr *HostResponse, req *FirmwareQueryReq) error {
	pbResp, ok := hr.Message.(*ctlpb.FirmwareQueryResp)
	if !ok {
		return errors.Errorf("unable to unpack message: %+v", hr.Message)
	}

	if req.SCM {
		if qr.HostSCMFirmware == nil {
			qr.HostSCMFirmware = make(HostSCMQueryMap)
		}
		scmResp, err := qr.getSCMResponse(pbResp)
		if err != nil {
			return err
		}

		qr.HostSCMFirmware[hr.Addr] = scmResp
	}

	if req.NVMe {
		if qr.HostNVMeFirmware == nil {
			qr.HostNVMeFirmware = make(HostNVMeQueryMap)
		}
		nvmeResp, err := qr.getNVMeResponse(pbResp)
		if err != nil {
			return err
		}

		qr.HostNVMeFirmware[hr.Addr] = nvmeResp
	}

	return nil
}

func (qr *FirmwareQueryResp) getSCMResponse(pbResp *ctlpb.FirmwareQueryResp) ([]*SCMQueryResult, error) {
	scmResults := make([]*SCMQueryResult, 0, len(pbResp.ScmResults))

	for _, pbScmRes := range pbResp.ScmResults {
		devResult := &SCMQueryResult{
			Info: &storage.ScmFirmwareInfo{
				ActiveVersion:     pbScmRes.ActiveVersion,
				StagedVersion:     pbScmRes.StagedVersion,
				ImageMaxSizeBytes: pbScmRes.ImageMaxSizeBytes,
				UpdateStatus:      storage.ScmFirmwareUpdateStatus(pbScmRes.UpdateStatus),
			},
		}
		if err := convert.Types(pbScmRes.Module, &devResult.Module); err != nil {
			return nil, errors.Wrapf(err, "unable to convert module")
		}
		if pbScmRes.Error != "" {
			devResult.Error = errors.New(pbScmRes.Error)
		}
		scmResults = append(scmResults, devResult)
	}

	return scmResults, nil
}

func (qr *FirmwareQueryResp) getNVMeResponse(pbResp *ctlpb.FirmwareQueryResp) ([]*NVMeQueryResult, error) {
	nvmeResults := make([]*NVMeQueryResult, 0, len(pbResp.NvmeResults))

	for _, pbNvmeRes := range pbResp.NvmeResults {
		devResult := &NVMeQueryResult{}
		if err := convert.Types(pbNvmeRes.Device, &devResult.Device); err != nil {
			return nil, errors.Wrapf(err, "unable to convert device")
		}
		nvmeResults = append(nvmeResults, devResult)
	}

	return nvmeResults, nil
}

// FirmwareQuery concurrently requests device firmware information from
// all hosts supplied in the request's hostlist, or all configured hosts
// if not explicitly specified. The function blocks until all results
// (successful or otherwise) are received, and returns a single response
// structure containing results for all host firmware query operations.
func FirmwareQuery(ctx context.Context, rpcClient UnaryInvoker, req *FirmwareQueryReq) (*FirmwareQueryResp, error) {
	if !req.SCM && !req.NVMe {
		return nil, errors.New("no device types requested")
	}

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return ctlpb.NewMgmtCtlClient(conn).FirmwareQuery(ctx, &ctlpb.FirmwareQueryReq{
			QueryScm:    req.SCM,
			QueryNvme:   req.NVMe,
			DeviceIDs:   req.Devices,
			ModelID:     req.ModelID,
			FirmwareRev: req.FirmwareRev,
		})
	})

	unaryResp, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	resp := new(FirmwareQueryResp)
	for _, hostResp := range unaryResp.Responses {
		if hostResp.Error != nil {
			if err := resp.addHostError(hostResp.Addr, hostResp.Error); err != nil {
				return nil, err
			}
			continue
		}

		if err := resp.addHostResponse(hostResp, req); err != nil {
			return nil, err
		}
	}

	return resp, nil
}

type (
	// DeviceType is an enum representing the storage device type.
	DeviceType uint32

	// FirmwareUpdateReq is a request to update firmware for a specific type
	// of storage device.
	FirmwareUpdateReq struct {
		unaryRequest
		FirmwarePath string
		Type         DeviceType
		Devices      []string // Specific devices to update
		ModelID      string   // Update only devices of specific model
		FirmwareRev  string   // Update only devices with a specific current firmware

	}

	// HostSCMUpdateMap maps a host name to a slice of SCM update results.
	HostSCMUpdateMap map[string][]*SCMUpdateResult

	// SCMUpdateResult represents the results of a firmware update
	// for a single SCM device.
	SCMUpdateResult struct {
		Module storage.ScmModule
		Error  error
	}

	// HostNVMeUpdateMap maps a host name to a slice of NVMe update results.
	HostNVMeUpdateMap map[string][]*NVMeUpdateResult

	// NVMeUpdateResult represents the results of a firmware update for a
	// single NVMe device.
	NVMeUpdateResult struct {
		DevicePCIAddr string
		Error         error
	}

	// FirmwareUpdateResp returns the results of firmware update operations.
	FirmwareUpdateResp struct {
		HostErrorsResp
		HostSCMResult  HostSCMUpdateMap
		HostNVMeResult HostNVMeUpdateMap
	}
)

const (
	// DeviceTypeUnknown represents an unspecified device type.
	DeviceTypeUnknown DeviceType = iota
	// DeviceTypeSCM represents SCM modules.
	DeviceTypeSCM
	// DeviceTypeNVMe represents NVMe SSDs.
	DeviceTypeNVMe
)

func (t DeviceType) toCtlPBType() (ctlpb.FirmwareUpdateReq_DeviceType, error) {
	switch t {
	case DeviceTypeSCM:
		return ctlpb.FirmwareUpdateReq_SCM, nil
	case DeviceTypeNVMe:
		return ctlpb.FirmwareUpdateReq_NVMe, nil
	}

	return ctlpb.FirmwareUpdateReq_DeviceType(-1),
		fmt.Errorf("invalid device type %d", uint32(t))
}

func (ur *FirmwareUpdateResp) addHostResponse(hr *HostResponse) error {
	pbResp, ok := hr.Message.(*ctlpb.FirmwareUpdateResp)
	if !ok {
		return errors.Errorf("unable to unpack message: %+v", hr.Message)
	}

	err := ur.addHostSCMResults(hr.Addr, pbResp)
	if err != nil {
		return err
	}

	return ur.addHostNVMeResults(hr.Addr, pbResp)
}

func (ur *FirmwareUpdateResp) addHostSCMResults(hostAddr string, pbResp *ctlpb.FirmwareUpdateResp) error {
	if len(pbResp.ScmResults) > 0 {
		if ur.HostSCMResult == nil {
			ur.HostSCMResult = make(HostSCMUpdateMap)
		}

		scmResults := make([]*SCMUpdateResult, 0, len(pbResp.ScmResults))

		for _, pbRes := range pbResp.ScmResults {
			devResult := &SCMUpdateResult{}
			if err := convert.Types(pbRes.Module, &devResult.Module); err != nil {
				return errors.Wrapf(err, "unable to convert module")
			}
			if pbRes.Error != "" {
				devResult.Error = errors.New(pbRes.Error)
			}
			scmResults = append(scmResults, devResult)
		}

		ur.HostSCMResult[hostAddr] = scmResults
	}

	return nil
}

func (ur *FirmwareUpdateResp) addHostNVMeResults(hostAddr string, pbResp *ctlpb.FirmwareUpdateResp) error {
	if len(pbResp.NvmeResults) > 0 {
		if ur.HostNVMeResult == nil {
			ur.HostNVMeResult = make(HostNVMeUpdateMap)
		}

		nvmeResults := make([]*NVMeUpdateResult, 0, len(pbResp.NvmeResults))

		for _, pbRes := range pbResp.NvmeResults {
			devResult := &NVMeUpdateResult{
				DevicePCIAddr: pbRes.PciAddr,
			}
			if pbRes.Error != "" {
				devResult.Error = errors.New(pbRes.Error)
			}
			nvmeResults = append(nvmeResults, devResult)
		}

		ur.HostNVMeResult[hostAddr] = nvmeResults
	}

	return nil
}

// Keys returns the sorted list of keys from the SCM result map.
func (m HostSCMUpdateMap) Keys() []string {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return keys
}

// Keys returns the sorted list of keys from the NVMe result map.
func (m HostNVMeUpdateMap) Keys() []string {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return keys
}

// FirmwareUpdate concurrently updates device firmware for a given device type
// for all hosts supplied in the request's hostlist, or all configured hosts
// if not explicitly specified. The function blocks until all results
// (successful or otherwise) are received, and returns a single response
// structure containing results for all host firmware update operations.
func FirmwareUpdate(ctx context.Context, rpcClient UnaryInvoker, req *FirmwareUpdateReq) (*FirmwareUpdateResp, error) {
	if req.FirmwarePath == "" {
		return nil, errors.New("firmware file path missing")
	}
	pbType, err := req.Type.toCtlPBType()
	if err != nil {
		return nil, err
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return ctlpb.NewMgmtCtlClient(conn).FirmwareUpdate(ctx, &ctlpb.FirmwareUpdateReq{
			FirmwarePath: req.FirmwarePath,
			Type:         pbType,
			DeviceIDs:    req.Devices,
			ModelID:      req.ModelID,
			FirmwareRev:  req.FirmwareRev,
		})
	})

	unaryResp, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	resp := new(FirmwareUpdateResp)
	for _, hostResp := range unaryResp.Responses {
		if hostResp.Error != nil {
			if err := resp.addHostError(hostResp.Addr, hostResp.Error); err != nil {
				return nil, err
			}
			continue
		}

		if err := resp.addHostResponse(hostResp); err != nil {
			return nil, err
		}
	}

	return resp, nil
}
