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

	"github.com/pkg/errors"
	"golang.org/x/net/context"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	pb_types "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

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

func scmModulesToPB(mms []storage.ScmModule) (pbMms pb_types.ScmModules) {
	for _, c := range mms {
		pbMms = append(
			pbMms,
			&ctlpb.ScmModule{
				Loc: &ctlpb.ScmModule_Location{
					Channel:    c.ChannelID,
					Channelpos: c.ChannelPosition,
					Memctrlr:   c.ControllerID,
					Socket:     c.SocketID,
				},
				Physicalid: c.PhysicalID,
				Capacity:   c.Capacity,
			})
	}
	return
}

func scmNamespacesToPB(nss []storage.ScmNamespace) (pbNss pb_types.ScmNamespaces) {
	for _, ns := range nss {
		pbNss = append(pbNss,
			&ctlpb.PmemDevice{
				Uuid:     ns.UUID,
				Blockdev: ns.BlockDevice,
				Dev:      ns.Name,
				Numanode: ns.NumaNode,
				Size:     ns.Size,
			})
	}

	return
}

func (c *StorageControlService) doNvmePrepare(req *ctlpb.PrepareNvmeReq) (resp *ctlpb.PrepareNvmeResp) {
	resp = &ctlpb.PrepareNvmeResp{}
	msg := "Storage Prepare NVMe"
	err := c.NvmePrepare(NvmePrepareRequest{
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

	pbResp.State = newState(c.log, ctlpb.ResponseStatus_CTL_SUCCESS, "", info, msg)
	pbResp.Pmems = scmNamespacesToPB(resp.Namespaces)

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

	controllers, err := c.NvmeScan()
	if err != nil {
		resp.Nvme = &ctlpb.ScanNvmeResp{
			State: newState(c.log, ctlpb.ResponseStatus_CTL_ERR_NVME, err.Error(), "", msg+"NVMe"),
		}
	} else {
		resp.Nvme = &ctlpb.ScanNvmeResp{
			State:  newState(c.log, ctlpb.ResponseStatus_CTL_SUCCESS, "", "", msg+"NVMe"),
			Ctrlrs: controllers,
		}
	}

	result, err := c.scm.Scan(scm.ScanRequest{})
	if err != nil {
		resp.Scm = &ctlpb.ScanScmResp{
			State: newState(c.log, ctlpb.ResponseStatus_CTL_ERR_SCM, err.Error(), "", msg+"SCM"),
		}
	} else {
		msg += fmt.Sprintf("SCM (%s)", result.State)
		resp.Scm = &ctlpb.ScanScmResp{
			State: newState(c.log, ctlpb.ResponseStatus_CTL_SUCCESS, "", "", msg),
		}
		if len(result.Namespaces) > 0 {
			resp.Scm.Pmems = scmNamespacesToPB(result.Namespaces)
		} else {
			resp.Scm.Modules = scmModulesToPB(result.Modules)
		}
	}

	return resp, nil
}

// newMntRet creates and populates NVMe ctrlr result and logs error through newState.
func newMntRet(log logging.Logger, op string, mntPoint string, status ctlpb.ResponseStatus, errMsg string, infoMsg string) *ctlpb.ScmMountResult {
	return &ctlpb.ScmMountResult{
		Mntpoint: mntPoint,
		State:    newState(log, status, errMsg, infoMsg, "scm mount "+op),
	}
}

func (c *ControlService) scmFormat(scmCfg storage.ScmConfig, reformat bool) (*ctlpb.ScmMountResult, error) {
	var eMsg, iMsg string
	status := ctlpb.ResponseStatus_CTL_SUCCESS

	req, err := scm.CreateFormatRequest(scmCfg, reformat)
	if err != nil {
		return nil, errors.Wrap(err, "generate format request")
	}
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

	return newMntRet(c.log, "format", scmCfg.MountPoint, status, eMsg, iMsg), nil
}

// doFormat performs format on storage subsystems, populates response results
// in storage subsystem routines and broadcasts (closes channel) if successful.
func (c *ControlService) doFormat(i *IOServerInstance, reformat bool, resp *ctlpb.StorageFormatResp) error {
	const msgFormatErr = "failure formatting storage, check RPC response for details"
	needsSuperblock := true
	needsScmFormat := reformat
	// placeholder result indicating NVMe not yet formatted
	resp.Crets = pb_types.NvmeControllerResults{
		newCret(c.log, "format", "", ctlpb.ResponseStatus_CTL_ERR_NVME, msgBdevScmNotReady, ""),
	}

	c.log.Infof("formatting storage for I/O server instance %d (reformat: %t)",
		i.Index, reformat)

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
			return nil // don't continue if formatted and no reformat opt
		}
	}

	// When SCM format is required, format and populate response with result.
	if needsScmFormat {
		results := pb_types.ScmMountResults{}
		result, err := c.scmFormat(scmConfig, true)
		if err != nil {
			return errors.Wrap(err, "scm format") // return unexpected errors
		}
		results = append(results, result)
		resp.Mrets = results

		if results.HasErrors() {
			c.log.Error(msgFormatErr)
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

	results := pb_types.NvmeControllerResults{} // init actual NVMe format results

	// If no superblock exists, populate NVMe response with format results.
	if needsSuperblock {
		bdevConfig := i.bdevConfig()

		// A config with SCM and no block devices is valid.
		// TODO: pull protobuf specifics out of c.nvme into this file.
		if len(bdevConfig.DeviceList) > 0 {
			// TODO: return result to be in line with scmFormat
			c.nvme.Format(bdevConfig, &results)
		}
	}

	resp.Crets = results // overwrite with actual results

	if results.HasErrors() {
		c.log.Error(msgFormatErr)
	} else {
		// TODO: remove use of nvme formatted flag to be consistent with scm
		c.nvme.formatted = true
		i.NotifyStorageReady()
	}

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
