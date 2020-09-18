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

package server

import (
	"context"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

// FirmwareQuery implements the method defined for the control service if
// firmware management is enabled for this build.
//
// It fetches information about the device firmware on this server based on the
// caller's request parameters. It can fetch firmware information for NVMe, SCM,
// or both.
func (svc *ControlService) FirmwareQuery(parent context.Context, pbReq *ctlpb.FirmwareQueryReq) (*ctlpb.FirmwareQueryResp, error) {
	svc.log.Debug("received FirmwareQuery RPC")

	pbResp := new(ctlpb.FirmwareQueryResp)

	if pbReq.QueryScm {
		scmResults, err := svc.querySCMFirmware()
		if err != nil {
			return nil, err
		}
		pbResp.ScmResults = scmResults
	}

	if pbReq.QueryNvme {
		nvmeResults, err := svc.queryNVMeFirmware()
		if err != nil {
			return nil, err
		}
		pbResp.NvmeResults = nvmeResults
	}

	svc.log.Debug("responding to FirmwareQuery RPC")
	return pbResp, nil
}

func (svc *ControlService) querySCMFirmware() ([]*ctlpb.ScmFirmwareQueryResp, error) {
	queryResp, err := svc.scm.QueryFirmware(scm.FirmwareQueryRequest{})
	if err != nil {
		return nil, err
	}

	scmResults := make([]*ctlpb.ScmFirmwareQueryResp, 0, len(queryResp.Results))
	for _, res := range queryResp.Results {
		pbResult := &ctlpb.ScmFirmwareQueryResp{}
		if err := convert.Types(res.Module, &pbResult.Module); err != nil {
			return nil, errors.Wrap(err, "unable to convert SCM module")
		}
		if res.Info != nil {
			pbResult.ActiveVersion = res.Info.ActiveVersion
			pbResult.StagedVersion = res.Info.StagedVersion
			pbResult.ImageMaxSizeBytes = res.Info.ImageMaxSizeBytes
			pbResult.UpdateStatus = uint32(res.Info.UpdateStatus)
		}
		pbResult.Error = res.Error
		scmResults = append(scmResults, pbResult)
	}

	return scmResults, nil
}

func (svc *ControlService) queryNVMeFirmware() ([]*ctlpb.NvmeFirmwareQueryResp, error) {
	scanResp, err := svc.NvmeScan(bdev.ScanRequest{})
	if err != nil {
		return nil, err
	}

	nvmeResults := make([]*ctlpb.NvmeFirmwareQueryResp, 0, len(scanResp.Controllers))
	for _, ctrlr := range scanResp.Controllers {
		pbResult := &ctlpb.NvmeFirmwareQueryResp{}
		if err := convert.Types(ctrlr, &pbResult.Device); err != nil {
			return nil, errors.Wrap(err, "unable to convert NVMe controller")
		}

		nvmeResults = append(nvmeResults, pbResult)
	}

	return nvmeResults, nil
}

// FirmwareUpdate implements the method defined for the control service if
// firmware management is enabled for this build.
//
// It updates the firmware on the storage devices of the specified type.
func (svc *ControlService) FirmwareUpdate(parent context.Context, pbReq *ctlpb.FirmwareUpdateReq) (*ctlpb.FirmwareUpdateResp, error) {
	svc.log.Debug("received FirmwareUpdate RPC")

	instances := svc.harness.Instances()
	for _, srv := range instances {
		if srv.isStarted() {
			rank, err := srv.GetRank()
			if err != nil {
				return nil, errors.New("unidentified server rank is running")
			}
			return nil, FaultInstancesNotStopped("firmware update", rank)
		}
	}

	pbResp := new(ctlpb.FirmwareUpdateResp)
	var err error
	switch pbReq.Type {
	case ctlpb.FirmwareUpdateReq_SCM:
		err = svc.updateSCM(pbReq, pbResp)
	case ctlpb.FirmwareUpdateReq_NVMe:
		err = svc.updateNVMe(pbReq, pbResp)
	default:
		err = errors.New("unrecognized device type")
	}

	if err != nil {
		return nil, err
	}

	svc.log.Debug("responding to FirmwareUpdate RPC")
	return pbResp, nil
}

func (svc *ControlService) updateSCM(pbReq *ctlpb.FirmwareUpdateReq, pbResp *ctlpb.FirmwareUpdateResp) error {
	updateResp, err := svc.scm.UpdateFirmware(scm.FirmwareUpdateRequest{
		FirmwarePath: pbReq.FirmwarePath,
	})
	if err != nil {
		return err
	}

	pbResp.ScmResults = make([]*ctlpb.ScmFirmwareUpdateResp, 0, len(updateResp.Results))
	for _, res := range updateResp.Results {
		pbRes := &ctlpb.ScmFirmwareUpdateResp{}
		if err := convert.Types(res, pbRes); err != nil {
			return err
		}
		pbRes.Error = res.Error
		pbResp.ScmResults = append(pbResp.ScmResults, pbRes)
	}
	return nil
}

func (svc *ControlService) updateNVMe(pbReq *ctlpb.FirmwareUpdateReq, pbResp *ctlpb.FirmwareUpdateResp) error {
	updateResp, err := svc.bdev.UpdateFirmware(bdev.FirmwareUpdateRequest{
		FirmwarePath: pbReq.FirmwarePath,
	})
	if err != nil {
		return err
	}

	pbResp.NvmeResults = make([]*ctlpb.NvmeFirmwareUpdateResp, 0, len(updateResp.Results))
	for _, res := range updateResp.Results {
		pbRes := &ctlpb.NvmeFirmwareUpdateResp{
			PciAddr: res.Device.PciAddr,
			Error:   res.Error,
		}
		pbResp.NvmeResults = append(pbResp.NvmeResults, pbRes)
	}
	return nil
}
