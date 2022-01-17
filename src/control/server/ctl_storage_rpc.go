//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"fmt"
	"os/user"

	"github.com/pkg/errors"
	"golang.org/x/net/context"

	"github.com/daos-stack/daos/src/control/common/proto"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	msgFormatErr      = "instance %d: failure formatting storage, check RPC response for details"
	msgNvmeFormatSkip = "NVMe format skipped on instance %d as SCM format did not complete"
)

// newResponseState creates, populates and returns ResponseState.
func newResponseState(inErr error, badStatus ctlpb.ResponseStatus, infoMsg string) *ctlpb.ResponseState {
	rs := new(ctlpb.ResponseState)
	rs.Info = infoMsg

	if inErr != nil {
		rs.Status = badStatus
		rs.Error = inErr.Error()
	}

	return rs
}

// stripNvmeDetails removes all controller details leaving only PCI address and
// NUMA node/socket ID. Useful when scanning only device topology.
func stripNvmeDetails(pbc *ctlpb.NvmeController) {
	pbc.Serial = ""
	pbc.Model = ""
	pbc.FwRev = ""
}

// newScanBdevResp populates protobuf NVMe scan response with controller info
// including health statistics or metadata if requested.
func newScanNvmeResp(req *ctlpb.ScanNvmeReq, inResp *storage.BdevScanResponse, inErr error) (*ctlpb.ScanNvmeResp, error) {
	outResp := new(ctlpb.ScanNvmeResp)
	outResp.State = new(ctlpb.ResponseState)

	if inErr != nil {
		outResp.State = newResponseState(inErr, ctlpb.ResponseStatus_CTL_ERR_NVME, "")
		return outResp, nil
	}

	pbCtrlrs := make(proto.NvmeControllers, 0, len(inResp.Controllers))
	if err := pbCtrlrs.FromNative(inResp.Controllers); err != nil {
		return nil, err
	}

	// trim unwanted fields so responses can be coalesced from hash map
	for _, pbc := range pbCtrlrs {
		if !req.GetHealth() {
			pbc.HealthStats = nil
		}
		if !req.GetMeta() {
			pbc.SmdDevices = nil
		}
		if req.GetBasic() {
			stripNvmeDetails(pbc)
		}
	}

	outResp.Ctrlrs = pbCtrlrs

	return outResp, nil
}

// scanBdevs updates transient details if health statistics or server metadata
// is requested otherwise just retrieves cached static controller details.
func (c *ControlService) scanBdevs(ctx context.Context, req *ctlpb.ScanNvmeReq) (*ctlpb.ScanNvmeResp, error) {
	if req == nil {
		return nil, errors.New("nil bdev request")
	}

	var bdevsInCfg bool
	for _, ei := range c.harness.Instances() {
		if ei.HasBlockDevices() {
			bdevsInCfg = true
		}
	}
	if !bdevsInCfg {
		c.log.Debugf("no bdevs in cfg so scan all")
		// return details of all bdevs if none are assigned to engines
		resp, err := c.storage.ScanBdevs(storage.BdevScanRequest{
			BypassCache: true,
		})

		return newScanNvmeResp(req, resp, err)
	}

	c.log.Debugf("scanning only bdevs in cfg")
	resp, err := c.scanAssignedBdevs(ctx, req.GetHealth() || req.GetMeta())

	return newScanNvmeResp(req, resp, err)
}

// newScanScmResp sets protobuf SCM scan response with module or namespace info.
func newScanScmResp(inResp *storage.ScmScanResponse, inErr error) (*ctlpb.ScanScmResp, error) {
	outResp := new(ctlpb.ScanScmResp)
	outResp.State = new(ctlpb.ResponseState)

	if inErr != nil {
		outResp.State = newResponseState(inErr, ctlpb.ResponseStatus_CTL_ERR_SCM, "")
		return outResp, nil
	}

	if len(inResp.Namespaces) == 0 {
		outResp.Modules = make(proto.ScmModules, 0, len(inResp.Modules))
		if err := (*proto.ScmModules)(&outResp.Modules).FromNative(inResp.Modules); err != nil {
			return nil, err
		}

		return outResp, nil
	}

	outResp.Namespaces = make(proto.ScmNamespaces, 0, len(inResp.Namespaces))
	if err := (*proto.ScmNamespaces)(&outResp.Namespaces).FromNative(inResp.Namespaces); err != nil {
		return nil, err
	}

	return outResp, nil
}

// scanScm will return mount details and usage for either emulated RAM or real PMem.
func (c *ControlService) scanScm(ctx context.Context, req *ctlpb.ScanScmReq) (*ctlpb.ScanScmResp, error) {
	if req == nil {
		return nil, errors.New("nil scm request")
	}

	// scan SCM, rescan scm storage details by default
	scmReq := storage.ScmScanRequest{Rescan: true}
	ssr, scanErr := c.ScmScan(scmReq)

	if scanErr != nil || !req.GetUsage() {
		return newScanScmResp(ssr, scanErr)
	}

	return newScanScmResp(c.getScmUsage(ssr))
}

// StorageScan discovers non-volatile storage hardware on node.
func (c *ControlService) StorageScan(ctx context.Context, req *ctlpb.StorageScanReq) (*ctlpb.StorageScanResp, error) {
	c.log.Debugf("received StorageScan RPC %v", req)

	if req == nil {
		return nil, errors.New("nil request")
	}
	resp := new(ctlpb.StorageScanResp)

	respNvme, err := c.scanBdevs(ctx, req.Nvme)
	if err != nil {
		return nil, err
	}
	resp.Nvme = respNvme

	respScm, err := c.scanScm(ctx, req.Scm)
	if err != nil {
		return nil, err
	}
	resp.Scm = respScm

	hpi, err := c.getHugePageInfo()
	if err != nil {
		return nil, err
	}
	if err := convert.Types(hpi, &resp.HugePageInfo); err != nil {
		return nil, err
	}

	c.log.Debug("responding to StorageScan RPC")

	return resp, nil
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
	instances := c.harness.Instances()
	resp := new(ctlpb.StorageFormatResp)
	resp.Mrets = make([]*ctlpb.ScmMountResult, 0, len(instances))
	resp.Crets = make([]*ctlpb.NvmeControllerResult, 0, len(instances))
	scmChan := make(chan *ctlpb.ScmMountResult, len(instances))

	c.log.Debugf("received StorageFormat RPC %v", req)

	// TODO: enable per-instance formatting
	formatting := 0
	for _, ei := range instances {
		formatting++
		go func(e Engine) {
			scmChan <- e.StorageFormatSCM(ctx, req.Reformat)
		}(ei)
	}

	instanceErrored := make(map[uint32]string)
	for formatting > 0 {
		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		case scmResult := <-scmChan:
			formatting--
			state := scmResult.GetState()
			if state.GetStatus() != ctlpb.ResponseStatus_CTL_SUCCESS {
				instanceErrored[scmResult.GetInstanceidx()] = state.GetError()
			}
			resp.Mrets = append(resp.Mrets, scmResult)
		}
	}

	// allow format to complete on one instance even if another fail
	// TODO: perform bdev format in parallel
	for _, ei := range instances {
		if _, hasError := instanceErrored[ei.Index()]; hasError {
			// if scm errored, indicate skipping bdev format
			ret := ei.newCret("", nil)
			ret.State.Info = fmt.Sprintf(msgNvmeFormatSkip, ei.Index())
			resp.Crets = append(resp.Crets, ret)
			continue
		}

		// SCM formatted correctly on this instance, format NVMe
		cResults := ei.StorageFormatNVMe()
		if cResults.HasErrors() {
			instanceErrored[ei.Index()] = cResults.Errors()
			resp.Crets = append(resp.Crets, cResults...)
			continue
		}

		if err := ei.StorageWriteNvmeConfig(ctx); err != nil {
			instanceErrored[ei.Index()] = err.Error()
			cResults = append(cResults, ei.newCret("", err))
		}

		resp.Crets = append(resp.Crets, cResults...)
	}

	// Notify storage ready for instances formatted without error.
	// Block until all instances have formatted NVMe to avoid
	// VFIO device or resource busy when starting I/O Engines
	// because devices have already been claimed during format.
	for _, ei := range instances {
		if msg, hasError := instanceErrored[ei.Index()]; hasError {
			c.log.Errorf("instance %d: %s", ei.Index(), msg)
			continue
		}
		ei.NotifyStorageReady()
	}

	return resp, nil
}

// StorageNvmeRebind rebinds SSD from kernel and binds to user-space to allow DAOS to use it.
func (c *ControlService) StorageNvmeRebind(ctx context.Context, req *ctlpb.NvmeRebindReq) (*ctlpb.NvmeRebindResp, error) {
	c.log.Debugf("received StorageNvmeRebind RPC %v", req)

	if req == nil {
		return nil, errors.New("nil request")
	}

	cu, err := user.Current()
	if err != nil {
		return nil, errors.Wrap(err, "get username")
	}

	prepReq := storage.BdevPrepareRequest{
		// zero as hugepages already allocated on start-up
		HugePageCount: 0,
		TargetUser:    cu.Username,
		PCIAllowList:  req.PciAddr,
		Reset_:        false,
	}

	resp := new(ctlpb.NvmeRebindResp)
	if _, err := c.NvmePrepare(prepReq); err != nil {
		err = errors.Wrap(err, "nvme rebind")
		c.log.Error(err.Error())

		resp.State = &ctlpb.ResponseState{
			Error:  err.Error(),
			Status: ctlpb.ResponseStatus_CTL_ERR_NVME,
		}

		return resp, nil // report prepare call result in response
	}

	c.log.Debug("responding to StorageNvmeRebind RPC")

	return resp, nil
}
