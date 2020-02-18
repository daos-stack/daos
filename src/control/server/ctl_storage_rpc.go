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

package server

import (
	"fmt"
	"strings"

	"github.com/pkg/errors"
	"golang.org/x/net/context"

	"github.com/daos-stack/daos/src/control/common/proto"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

const msgFormatErr = "failure formatting storage, check RPC response for details"

// newState creates, populates and returns ResponseState in addition
// to logging any err.
func newState(log logging.Logger, status ctlpb.ResponseStatus, errMsg string, infoMsg string,
	contextMsg string) *ctlpb.ResponseState {

	state := &ctlpb.ResponseState{
		Status: status, Error: errMsg, Info: infoMsg,
	}

	if errMsg != "" {
		log.Error(contextMsg + ": " + errMsg)
	}

	return state
}

func (c *StorageControlService) doNvmePrepare(req *ctlpb.PrepareNvmeReq) (resp *ctlpb.PrepareNvmeResp) {
	resp = &ctlpb.PrepareNvmeResp{}
	msg := "Storage Prepare NVMe"
	_, err := c.NvmePrepare(bdev.PrepareRequest{
		HugePageCount: int(req.GetNrhugepages()),
		TargetUser:    req.GetTargetuser(),
		PCIWhitelist:  req.GetPciwhitelist(),
		ResetOnly:     req.GetReset_(),
	})

	if err != nil {
		resp.State = newState(c.log, ctlpb.ResponseStatus_CTL_ERR_NVME, err.Error(), "", msg)
		return
	}

	resp.State = newState(c.log, ctlpb.ResponseStatus_CTL_SUCCESS, "", "", msg)
	return
}

func (c *StorageControlService) doScmPrepare(pbReq *ctlpb.PrepareScmReq) (pbResp *ctlpb.PrepareScmResp) {
	pbResp = &ctlpb.PrepareScmResp{}
	msg := "Storage Prepare SCM"

	scmState, err := c.GetScmState()
	if err != nil {
		pbResp.State = newState(c.log, ctlpb.ResponseStatus_CTL_ERR_SCM, err.Error(), "", msg)
		return
	}
	c.log.Debugf("SCM state before prep: %s", scmState)

	resp, err := c.ScmPrepare(scm.PrepareRequest{Reset: pbReq.Reset_})
	if err != nil {
		pbResp.State = newState(c.log, ctlpb.ResponseStatus_CTL_ERR_SCM, err.Error(), "", msg)
		return
	}

	info := ""
	if resp.RebootRequired {
		info = scm.MsgScmRebootRequired
	}

	pbResp.Namespaces = make(proto.ScmNamespaces, 0, len(resp.Namespaces))
	if err := (*proto.ScmNamespaces)(&pbResp.Namespaces).FromNative(resp.Namespaces); err != nil {
		pbResp.State = newState(c.log, ctlpb.ResponseStatus_CTL_ERR_SCM, err.Error(), "", msg)
		return
	}
	pbResp.State = newState(c.log, ctlpb.ResponseStatus_CTL_SUCCESS, "", info, msg)

	return
}

// StoragePrepare configures SSDs for user specific access with SPDK and
// groups SCM modules in AppDirect/interleaved mode as kernel "pmem" devices.
func (c *StorageControlService) StoragePrepare(ctx context.Context, req *ctlpb.StoragePrepareReq) (
	*ctlpb.StoragePrepareResp, error) {

	c.log.Debug("received StoragePrepare RPC; proceeding to instance storage preparation")

	resp := &ctlpb.StoragePrepareResp{}

	if req.Nvme != nil {
		resp.Nvme = c.doNvmePrepare(req.Nvme)
	}
	if req.Scm != nil {
		resp.Scm = c.doScmPrepare(req.Scm)
	}

	return resp, nil
}

// StorageScan discovers non-volatile storage hardware on node.
func (c *StorageControlService) StorageScan(ctx context.Context, req *ctlpb.StorageScanReq) (*ctlpb.StorageScanResp, error) {
	c.log.Debug("received StorageScan RPC")

	msg := "Storage Scan "
	resp := new(ctlpb.StorageScanResp)

	bsr, err := c.bdev.Scan(bdev.ScanRequest{})
	if err != nil {
		resp.Nvme = &ctlpb.ScanNvmeResp{
			State: newState(c.log, ctlpb.ResponseStatus_CTL_ERR_NVME, err.Error(), "", msg+"NVMe"),
		}
	} else {
		pbCtrlrs := make(proto.NvmeControllers, 0, len(bsr.Controllers))
		if err := pbCtrlrs.FromNative(bsr.Controllers); err != nil {
			c.log.Errorf("failed to cleanly convert %#v to protobuf: %s", bsr.Controllers, err)
		}
		resp.Nvme = &ctlpb.ScanNvmeResp{
			State:  newState(c.log, ctlpb.ResponseStatus_CTL_SUCCESS, "", "", msg+"NVMe"),
			Ctrlrs: pbCtrlrs,
		}
	}

	ssr, err := c.scm.Scan(scm.ScanRequest{})
	if err != nil {
		resp.Scm = &ctlpb.ScanScmResp{
			State: newState(c.log, ctlpb.ResponseStatus_CTL_ERR_SCM, err.Error(), "", msg+"SCM"),
		}
	} else {
		msg += fmt.Sprintf("SCM (%s)", ssr.State)
		resp.Scm = &ctlpb.ScanScmResp{
			State: newState(c.log, ctlpb.ResponseStatus_CTL_SUCCESS, "", "", msg),
		}
		if len(ssr.Namespaces) > 0 {
			resp.Scm.Namespaces = make(proto.ScmNamespaces, 0, len(ssr.Namespaces))
			err := (*proto.ScmNamespaces)(&resp.Scm.Namespaces).FromNative(ssr.Namespaces)
			if err != nil {
				resp.Scm.State = newState(c.log, ctlpb.ResponseStatus_CTL_ERR_SCM,
					err.Error(), "", msg+"SCM")
			}
		} else {
			resp.Scm.Modules = make(proto.ScmModules, 0, len(ssr.Modules))
			err := (*proto.ScmModules)(&resp.Scm.Modules).FromNative(ssr.Modules)
			if err != nil {
				resp.Scm.State = newState(c.log, ctlpb.ResponseStatus_CTL_ERR_SCM,
					err.Error(), "", msg+"SCM")
			}
		}
	}

	c.log.Debug("responding to StorageScan RPC")

	return resp, nil
}

// newMntRet creates and populates NVMe ctrlr result and logs error through newState.
func newMntRet(log logging.Logger, op, mntPoint string, status ctlpb.ResponseStatus, errMsg, infoMsg string) *ctlpb.ScmMountResult {
	if mntPoint == "" {
		mntPoint = "<nil>"
	}
	return &ctlpb.ScmMountResult{
		Mntpoint: mntPoint,
		State:    newState(log, status, errMsg, infoMsg, "scm mount "+op),
	}
}

// newCret creates and populates NVMe controller result and logs error
func newCret(log logging.Logger, op, pciAddr string, status ctlpb.ResponseStatus, errMsg, infoMsg string) *ctlpb.NvmeControllerResult {
	if pciAddr == "" {
		pciAddr = "<nil>"
	}

	return &ctlpb.NvmeControllerResult{
		Pciaddr: pciAddr,
		State:   newState(log, status, errMsg, infoMsg, "nvme controller "+op),
	}
}

func (c *ControlService) scmFormat(scmCfg storage.ScmConfig, reformat bool) (*ctlpb.ScmMountResult, error) {
	var eMsg, iMsg string
	status := ctlpb.ResponseStatus_CTL_SUCCESS

	req, err := scm.CreateFormatRequest(scmCfg, reformat)
	if err != nil {
		return nil, errors.Wrap(err, "generate format request")
	}

	scmStr := fmt.Sprintf("SCM (%s:%s)", scmCfg.Class, scmCfg.MountPoint)
	c.log.Infof("Starting format of %s", scmStr)
	res, err := c.scm.Format(*req)
	if err != nil {
		eMsg = err.Error()
		iMsg = fault.ShowResolutionFor(err)
		status = ctlpb.ResponseStatus_CTL_ERR_SCM
	} else if !res.Formatted {
		err = scm.FaultUnknown
		eMsg = errors.WithMessage(err, "is still unformatted").Error()
		iMsg = fault.ShowResolutionFor(err)
		status = ctlpb.ResponseStatus_CTL_ERR_SCM
	}

	if err != nil {
		c.log.Errorf("  format of %s failed: %s", scmStr, err)
	}
	c.log.Infof("Finished format of %s", scmStr)

	return newMntRet(c.log, "format", scmCfg.MountPoint, status, eMsg, iMsg), nil
}

// doFormat performs format on storage subsystems, populates response results
// in storage subsystem routines and broadcasts (closes channel) if successful.
func (c *ControlService) doFormat(i *IOServerInstance, reformat bool, resp *ctlpb.StorageFormatResp) error {
	needsSuperblock := true
	needsScmFormat := reformat
	// indicate that NVMe not yet formatted
	skipErr := FaultBdevFormatSkipped(i.Index())
	skipNvmeResult := newCret(c.log, "format", "", ctlpb.ResponseStatus_CTL_ERR_NVME,
		skipErr.Error(), skipErr.Resolution)

	c.log.Infof("formatting storage for %s instance %d (reformat: %t)",
		DataPlaneName, i.Index(), reformat)

	scmConfig := i.scmConfig()

	// If not reformatting, check if SCM is already formatted.
	if !reformat {
		var err error
		needsScmFormat, err = i.NeedsScmFormat()
		if err != nil {
			return errors.Wrap(err, "unable to check storage formatting")
		}
		if !needsScmFormat {
			err = scm.FaultFormatNoReformat
			resp.Mrets = append(resp.Mrets,
				newMntRet(c.log, "format", scmConfig.MountPoint,
					ctlpb.ResponseStatus_CTL_ERR_SCM, err.Error(),
					fault.ShowResolutionFor(err)))
			resp.Crets = append(resp.Crets, skipNvmeResult)

			return nil // don't continue if formatted and no reformat opt
		}
	}

	// When SCM format is required, format and append to response results.
	if needsScmFormat {
		result, err := c.scmFormat(scmConfig, true)
		if err != nil {
			return errors.Wrap(err, "scm format") // return unexpected errors
		}
		resp.Mrets = append(resp.Mrets, result)

		if result.State.Status != ctlpb.ResponseStatus_CTL_SUCCESS {
			c.log.Error(msgFormatErr)
			resp.Crets = append(resp.Crets, skipNvmeResult)

			return nil // don't continue if we can't format SCM
		}
	} else {
		var err error
		// If SCM was already formatted, verify if superblock exists.
		needsSuperblock, err = i.NeedsSuperblock()
		if err != nil {
			return errors.Wrap(err, "unable to check instance superblock")
		}
	}

	// If no superblock exists, format NVMe and populate response with results.
	if needsSuperblock {
		nvmeResults := proto.NvmeControllerResults{}
		bdevConfig := i.bdevConfig()

		// A config with SCM and no block devices is valid.
		if len(bdevConfig.DeviceList) > 0 {
			bdevListStr := strings.Join(bdevConfig.DeviceList, ",")
			c.log.Infof("Starting format of %s block devices (%s)", bdevConfig.Class, bdevListStr)

			res, err := c.bdev.Format(bdev.FormatRequest{
				Class:      bdevConfig.Class,
				DeviceList: bdevConfig.DeviceList,
			})
			if err != nil {
				return err
			}

			for dev, status := range res.DeviceResponses {
				var errMsg, infoMsg string
				ctlpbStatus := ctlpb.ResponseStatus_CTL_SUCCESS
				if status.Error != nil {
					ctlpbStatus = ctlpb.ResponseStatus_CTL_ERR_NVME
					errMsg = status.Error.Error()
					c.log.Errorf("  format of %s device %s failed: %s", bdevConfig.Class, dev, errMsg)
					if fault.HasResolution(status.Error) {
						infoMsg = fault.ShowResolutionFor(status.Error)
					}
				}
				nvmeResults = append(nvmeResults,
					newCret(c.log, "format", dev, ctlpbStatus, errMsg, infoMsg))
			}

			c.log.Infof("Finished format of %s block devices (%s)", bdevConfig.Class, bdevListStr)
		}

		resp.Crets = append(resp.Crets, nvmeResults...) // append this instance's results

		if nvmeResults.HasErrors() {
			c.log.Error(msgFormatErr)
			return nil // don't continue if we can't format NVMe
		}
	}

	i.NotifyStorageReady()

	return nil
}

// StorageFormat delegates to Storage implementation's Format methods to prepare
// storage for use by DAOS data plane.
//
// Errors returned will stop other servers from formatting, non-fatal errors
// specific to particular device should be reported within resp results instead.
//
// Send response containing multiple results of format operations on scm mounts
// and nvme controllers.
func (c *ControlService) StorageFormat(req *ctlpb.StorageFormatReq, stream ctlpb.MgmtCtl_StorageFormatServer) error {
	resp := new(ctlpb.StorageFormatResp)
	resp.Mrets = proto.ScmMountResults{}
	resp.Crets = proto.NvmeControllerResults{}

	c.log.Debugf("received StorageFormat RPC %v; proceeding to instance storage format", req)

	// TODO: We may want to ease this restriction at some point, but having this
	// here for now should help to cut down on shenanigans which might result
	// in data loss.
	if c.harness.IsStarted() {
		return errors.New("cannot format storage with running I/O server instances")
	}

	// temporary scaffolding
	for _, i := range c.harness.Instances() {
		if err := c.doFormat(i, req.Reformat, resp); err != nil {
			return errors.WithMessage(err, "formatting storage")
		}
	}

	if err := stream.Send(resp); err != nil {
		return errors.WithMessagef(err, "sending response (%+v)", resp)
	}

	return nil
}
