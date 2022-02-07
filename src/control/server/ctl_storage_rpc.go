//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

/*
#include "daos_srv/rsvc.h"
*/
import "C"

import (
	"fmt"
	"os/user"
	"strconv"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"
	"golang.org/x/net/context"

	"github.com/daos-stack/daos/src/control/common/proto"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/server/engine"
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

// Adjust the NVME available size to its real usable size.
func (c *ControlService) adjustNvmeSize(resp *ctlpb.ScanNvmeResp) {
	for _, ctl := range resp.GetCtrlrs() {
		for _, dev := range ctl.GetSmdDevices() {
			if dev.GetDevState() != "NORMAL" {
				continue
			}

			targetCount := uint64(len(dev.GetTgtIds()))
			clusterSmdCount := dev.GetAvailBytes() / dev.GetClusterSize()
			clusterTargetCount := clusterSmdCount / targetCount
			availBytes := clusterTargetCount * targetCount
			availBytes *= dev.GetClusterSize()
			c.log.Infof("removing %s (%d B) storage from SMD device %s (%s)",
				humanize.Bytes(dev.GetAvailBytes()-availBytes),
				dev.GetAvailBytes()-availBytes,
				dev.GetUuid(), ctl.GetPciAddr())
			dev.AvailBytes = availBytes
		}
	}
}

// return the size of the ram disk file used for managing SCM metadata
func (c *ControlService) getMetadataCapacity(mountPoint string) (uint64, error) {
	var engineCfg *engine.Config
loop:
	for index := range(c.srvCfg.Engines) {
		for _, tierCfg := range c.srvCfg.Engines[index].Storage.Tiers {
			if ! tierCfg.IsSCM() || tierCfg.Scm.MountPoint != mountPoint {
				continue
			}

			engineCfg = c.srvCfg.Engines[index]
			break loop
		}
	}

	if engineCfg == nil {
		return 0, errors.Errorf("unknown SCM mount point %s", mountPoint)
	}

	mdCapStr, err := engineCfg.GetEnvVar(C.DAOS_MD_CAP_ENV)
	if err != nil {
		c.log.Debugf("using default metadata capacity with SCM %s: %s (%d B)",
			mountPoint, humanize.Bytes(C.DEFAULT_DAOS_MD_CAP_SIZE), C.DEFAULT_DAOS_MD_CAP_SIZE)
		return uint64(C.DEFAULT_DAOS_MD_CAP_SIZE), nil
	}

	mdCap, err := strconv.ParseUint(mdCapStr, 10, 64)
	if err != nil {
		return 0, errors.Wrapf(err,
			"invalid metadata capacity: %q does not define a plain int", mdCapStr)
	}
	mdCap = mdCap << 20
	c.log.Debugf("using custom metadata capacity with SCM %s: %s (%d B)",
		mountPoint, humanize.Bytes(mdCap), mdCap)

	return mdCap, nil
}

// Adjust the SCM available size to the real usable size.
func (c *ControlService) adjustScmSize(resp *ctlpb.ScanScmResp) {
	const mdExt4Bytes uint64 = 1 << 20 // 1MiB (i.e 1048576B) min needed 36864B
	for _, scmNamespace := range resp.GetNamespaces() {
		mdBytes := mdExt4Bytes
		mdCapBytes, err := c.getMetadataCapacity(scmNamespace.GetMount().GetPath())
		if err != nil {
			c.log.Errorf("Skipping SCM %s: %s",
				scmNamespace.GetMount().GetPath(), err.Error())
			continue
		}
		mdBytes += mdCapBytes

		if mdBytes < scmNamespace.Mount.GetAvailBytes() {
			c.log.Infof("removing %s (%d B) storage from SCM %s",
				humanize.Bytes(mdBytes), mdBytes, scmNamespace.Mount.GetPath())
			scmNamespace.Mount.AvailBytes -= mdBytes
		} else {
			c.log.Errorf("no more storage space within SCM %s",
				scmNamespace.Mount.GetPath())
			scmNamespace.Mount.AvailBytes = 0
		}
	}
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
	if req.Nvme.GetMeta() {
		c.adjustNvmeSize(respNvme)
	}
	resp.Nvme = respNvme

	respScm, err := c.scanScm(ctx, req.Scm)
	if err != nil {
		return nil, err
	}
	if req.Scm.GetUsage() {
		c.adjustScmSize(respScm)
	}
	resp.Scm = respScm

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
