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
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

const (
	msgFormatErr      = "instance %d: failure formatting storage, check RPC response for details"
	msgNvmeFormatSkip = "NVMe format skipped on instance %d as SCM format did not complete"
)

// newState creates, populates and returns ResponseState in addition
// to logging any err.
func newState(log logging.Logger, status ctlpb.ResponseStatus, errMsg string, infoMsg string,
	contextMsg string) *ctlpb.ResponseState {

	state := &ctlpb.ResponseState{
		Status: status, Error: errMsg, Info: infoMsg,
	}

	if errMsg != "" {
		// TODO: is this necessary, maybe not?
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
	pbResp.Rebootrequired = resp.RebootRequired

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

// scmFormat will return either successful result or error.
func (srv *IOServerInstance) scmFormat(p *scm.Provider, reformat bool) (*ctlpb.ScmMountResult, error) {
	var eMsg, iMsg string
	srvIdx := srv.Index()
	cfg := srv.scmConfig()
	status := ctlpb.ResponseStatus_CTL_SUCCESS

	req, err := scm.CreateFormatRequest(cfg, reformat)
	if err != nil {
		return nil, errors.Wrap(err, "generate format request")
	}

	scmStr := fmt.Sprintf("SCM (%s:%s)", cfg.Class, cfg.MountPoint)
	srv.log.Infof("Instance %d: starting format of %s", srvIdx, scmStr)
	res, err := p.Format(*req)
	if err == nil && !res.Formatted {
		err = errors.WithMessage(scm.FaultUnknown, "is still unformatted")
	}

	if err != nil {
		srv.log.Errorf("  format of %s failed: %s", scmStr, err)
		return nil, err
	}
	srv.log.Infof("Instance %d: finished format of %s", srvIdx, scmStr)

	return newMntRet(srv.log, "format", cfg.MountPoint, status, eMsg, iMsg), nil
}

func (srv *IOServerInstance) bdevFormat(p *bdev.Provider) (results proto.NvmeControllerResults) {
	srvIdx := srv.Index()
	cfg := srv.bdevConfig()
	results = make(proto.NvmeControllerResults, 0, len(cfg.DeviceList))

	// A config with SCM and no block devices is valid.
	if len(cfg.DeviceList) == 0 {
		return
	}

	bdevListStr := strings.Join(cfg.DeviceList, ",")
	srv.log.Infof("Instance %d: starting format of %s block devices (%s)",
		srvIdx, cfg.Class, bdevListStr)

	res, err := p.Format(bdev.FormatRequest{
		Class:      cfg.Class,
		DeviceList: cfg.DeviceList,
	})
	if err != nil {
		results = append(results,
			newCret(srv.log, "format", "", ctlpb.ResponseStatus_CTL_ERR_NVME,
				err.Error(), fault.ShowResolutionFor(err)))
		return
	}

	for dev, status := range res.DeviceResponses {
		var errMsg, infoMsg string
		ctlpbStatus := ctlpb.ResponseStatus_CTL_SUCCESS
		if status.Error != nil {
			ctlpbStatus = ctlpb.ResponseStatus_CTL_ERR_NVME
			errMsg = status.Error.Error()
			srv.log.Errorf("  format of %s device %s failed: %s", cfg.Class, dev, errMsg)
			if fault.HasResolution(status.Error) {
				infoMsg = fault.ShowResolutionFor(status.Error)
			}
		}
		results = append(results,
			newCret(srv.log, "format", dev, ctlpbStatus, errMsg, infoMsg))
	}

	srv.log.Infof("Instance %d: finished format of %s block devices (%s)",
		srvIdx, cfg.Class, bdevListStr)

	return
}

// doFormat performs format on storage subsystems, populates response results
// in storage subsystem routines and broadcasts (closes channel) if successful.
func (srv *IOServerInstance) StorageFormat(reformat bool, scmProvider *scm.Provider, bdevProvider *bdev.Provider) (resp *ctlpb.StorageFormatResp) {
	resp = new(ctlpb.StorageFormatResp)
	resp.Mrets = proto.ScmMountResults{}
	resp.Crets = proto.NvmeControllerResults{}

	srvIdx := srv.Index()
	needsSuperblock := true
	needsScmFormat := reformat

	srv.log.Infof("Formatting storage for %s instance %d (reformat: %t)",
		DataPlaneName, srvIdx, reformat)

	var scmErr error
	defer func() {
		if scmErr != nil {
			srv.log.Errorf(msgFormatErr, srvIdx)
			resp.Mrets = append(resp.Mrets,
				newMntRet(srv.log, "format", srv.scmConfig().MountPoint,
					ctlpb.ResponseStatus_CTL_ERR_SCM, scmErr.Error(),
					fault.ShowResolutionFor(scmErr)))
			if len(srv.bdevConfig().DeviceList) > 0 {
				resp.Crets = append(resp.Crets,
					newCret(srv.log, "format", "",
						ctlpb.ResponseStatus_CTL_SUCCESS, "",
						fmt.Sprintf(msgNvmeFormatSkip, srvIdx)))
			}
		}
	}()

	if srv.isStarted() {
		scmErr = errors.Errorf("instance %d: can't format storage of running instance",
			srvIdx)
		return
	}

	// If not reformatting, check if SCM is already formatted.
	if !reformat {
		needsScmFormat, scmErr = srv.NeedsScmFormat()
		if scmErr == nil && !needsScmFormat {
			scmErr = scm.FaultFormatNoReformat
		}
		if scmErr != nil {
			return
		}
	}

	// When SCM format is required, format and append to response results.
	if needsScmFormat {
		var result *ctlpb.ScmMountResult
		result, scmErr = srv.scmFormat(scmProvider, true)
		if scmErr != nil {
			return
		}
		resp.Mrets = append(resp.Mrets, result)
	} else {
		// If SCM was already formatted, verify if superblock exists.
		needsSuperblock, scmErr = srv.NeedsSuperblock()
		if scmErr != nil {
			return
		}
	}

	// If no superblock exists, format NVMe and populate response with results.
	if needsSuperblock {
		nvmeResults := srv.bdevFormat(bdevProvider)

		resp.Crets = append(resp.Crets, nvmeResults...) // append this instance's results

		if nvmeResults.HasErrors() {
			srv.log.Errorf(msgFormatErr, srvIdx)
			return // don't continue if we can't format NVMe
		}
	}

	srv.NotifyStorageReady()

	return
}

// StorageFormat delegates to Storage implementation's Format methods to prepare
// storage for use by DAOS data plane.
//
// Errors returned will stop other servers from formatting, non-fatal errors
// specific to particular device should be reported within resp results instead.
//
// Send response containing multiple results of format operations on scm mounts
// and nvme controllers.
func (c *ControlService) StorageFormat(ctx context.Context, req *ctlpb.StorageFormatReq) (*ctlpb.StorageFormatResp, error) {
	var resp *ctlpb.StorageFormatResp
	respChan := make(chan *ctlpb.StorageFormatResp, len(c.harness.Instances()))

	c.log.Debugf("received StorageFormat RPC %v; proceeding to instance storage format", req)

	// TODO: enable per-instance formatting
	formatting := 0
	for _, srv := range c.harness.Instances() {
		formatting++
		go func(s *IOServerInstance) {
			respChan <- s.StorageFormat(req.Reformat, c.scm, c.bdev)
		}(srv)
	}

	for {
		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		case newResp := <-respChan:
			formatting--
			if resp == nil {
				resp = newResp
			} else {
				resp.Mrets = append(resp.Mrets, newResp.Mrets...)
				resp.Crets = append(resp.Crets, newResp.Crets...)
			}
			if formatting == 0 {
				return resp, nil
			}
		}
	}
}
