//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"fmt"
	"os/user"
	"path/filepath"
	"strings"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"
	"golang.org/x/net/context"

	"github.com/daos-stack/daos/src/control/common/proto"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/server/config"
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

// TODO: de-duplicate logic to populate prepare request from server config after
//       DAOS-7002 is completed
func updateNvmePrepareReq(req *storage.BdevPrepareRequest, cfg *config.Server) {
	if req.HugePageCount == 0 {
		req.HugePageCount = minHugePageCount
		if cfgHasBdevs(cfg) {
			// The config value is intended to be per-engine, so we
			// need to adjust based on the number of engines.
			req.HugePageCount = cfg.NrHugepages * len(cfg.Engines)
		}
	}
	if req.TargetUser == "" {
		if runningUser, err := user.Current(); err == nil {
			req.TargetUser = runningUser.Username
		}
	}
	if req.PCIAllowlist == "" {
		req.PCIAllowlist = strings.Join(cfg.BdevInclude, " ")
	}
	req.PCIBlocklist = strings.Join(cfg.BdevExclude, " ")
	req.DisableVFIO = cfg.DisableVFIO
	req.DisableVMD = cfg.DisableVMD || cfg.DisableVFIO || !iommuDetected()
}

// doNvmePrepare issues prepare request and returns response.
func (c *ControlService) doNvmePrepare(pbReq *ctlpb.PrepareNvmeReq) *ctlpb.PrepareNvmeResp {
	c.log.Debugf("performing nvme prep %v", pbReq)
	pnr := new(ctlpb.PrepareNvmeResp)

	req := storage.BdevPrepareRequest{
		HugePageCount: int(pbReq.GetNrHugePages()),
		TargetUser:    pbReq.GetTargetUser(),
		PCIAllowlist:  pbReq.GetPciAllowList(),
		ResetOnly:     pbReq.GetReset_(),
		// Default to minimum necessary for scan to work correctly.
	}

	if !req.ResetOnly {
		updateNvmePrepareReq(&req, c.srvCfg)
	}

	_, err := c.NvmePrepare(req)
	pnr.State = newResponseState(err, ctlpb.ResponseStatus_CTL_ERR_NVME, "")

	return pnr
}

// newPrepareScmResp sets protobuf SCM prepare response with results.
func newPrepareScmResp(inResp *storage.ScmPrepareResponse, inErr error) (*ctlpb.PrepareScmResp, error) {
	outResp := new(ctlpb.PrepareScmResp)
	outResp.State = new(ctlpb.ResponseState)

	if inErr != nil {
		outResp.State = newResponseState(inErr, ctlpb.ResponseStatus_CTL_ERR_SCM, "")
		return outResp, nil
	}

	if inResp.RebootRequired {
		outResp.Rebootrequired = true
		outResp.State.Info = storage.ScmMsgRebootRequired
	}

	outResp.Namespaces = make(proto.ScmNamespaces, 0, len(inResp.Namespaces))
	if err := (*proto.ScmNamespaces)(&outResp.Namespaces).FromNative(inResp.Namespaces); err != nil {
		return nil, err
	}

	return outResp, nil
}

func (c *ControlService) doScmPrepare(req *ctlpb.PrepareScmReq) (*ctlpb.PrepareScmResp, error) {
	c.log.Debugf("performing scm prep %v", req)

	scmState, err := c.GetScmState()
	if err != nil {
		return newPrepareScmResp(nil, err)
	}
	c.log.Debugf("SCM state before prep: %s", scmState)

	resp, err := c.ScmPrepare(storage.ScmPrepareRequest{Reset: req.Reset_})

	return newPrepareScmResp(resp, err)
}

// StoragePrepare configures resident host storage for use with DAOS, fails if
// harness engine instances have started.
func (c *ControlService) StoragePrepare(ctx context.Context, req *ctlpb.StoragePrepareReq) (*ctlpb.StoragePrepareResp, error) {
	c.log.Debugf("received StoragePrepare RPC %v", req)

	resp := new(ctlpb.StoragePrepareResp)

	for _, ei := range c.harness.Instances() {
		if ei.isStarted() {
			return nil, errors.Errorf("instance %d: can't prepare storage if running",
				ei.Index())
		}
	}

	if req.Nvme != nil {
		resp.Nvme = c.doNvmePrepare(req.Nvme)
	}
	if req.Scm != nil {
		respScm, err := c.doScmPrepare(req.Scm)
		if err != nil {
			return nil, err
		}
		resp.Scm = respScm
	}

	return resp, nil
}

// mapCtrlrs maps each controller to it's PCI address.
func mapCtrlrs(ctrlrs storage.NvmeControllers) (map[string]*storage.NvmeController, error) {
	ctrlrMap := make(map[string]*storage.NvmeController)

	for _, ctrlr := range ctrlrs {
		if _, exists := ctrlrMap[ctrlr.PciAddr]; exists {
			return nil, errors.Errorf("duplicate entries for controller %s",
				ctrlr.PciAddr)
		}

		ctrlrMap[ctrlr.PciAddr] = ctrlr
	}

	return ctrlrMap, nil
}

// scanInstanceBdevs retrieves up-to-date NVMe controller info including
// health statistics and stored server meta-data. If I/O Engines are running
// then query is issued over dRPC as go-spdk bindings cannot be used to access
// controller claimed by another process. Only update info for controllers
// assigned to I/O Engines.
func (c *ControlService) scanInstanceBdevs(ctx context.Context) (*storage.BdevScanResponse, error) {
	var ctrlrs storage.NvmeControllers
	instances := c.harness.Instances()

	for _, srv := range instances {
		if !srv.storage.HasBlockDevices() {
			continue
		}
		direct := !srv.isReady()

		tsrs, err := srv.storage.ScanBdevTiers(direct)
		if err != nil {
			return nil, err
		}

		if direct {
			for _, tsr := range tsrs {
				ctrlrs = ctrlrs.Update(tsr.Result.Controllers...)
			}
			continue
		}

		for _, tsr := range tsrs {
			ctrlrMap, err := mapCtrlrs(tsr.Result.Controllers)
			if err != nil {
				return nil, errors.Wrap(err, "create controller map")
			}

			// if io servers are active and have claimed the assigned devices,
			// query over drpc to update controller details with current health
			// stats and smd info
			if err := srv.updateInUseBdevs(ctx, ctrlrMap); err != nil {
				return nil, errors.Wrap(err, "updating bdev health and smd info")
			}

			ctrlrs = ctrlrs.Update(tsr.Result.Controllers...)
		}
	}

	return &storage.BdevScanResponse{Controllers: ctrlrs}, nil
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

	if req.Health || req.Meta {
		// filter results based on config file bdev_list contents
		resp, err := c.scanInstanceBdevs(ctx)

		return newScanNvmeResp(req, resp, err)
	}

	// return cached results for all bdevs
	resp, err := c.NvmeScan(storage.BdevScanRequest{})

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

func findPMemInScan(ssr *storage.ScmScanResponse, pmemDevs []string) *storage.ScmNamespace {
	for _, scanned := range ssr.Namespaces {
		for _, path := range pmemDevs {
			if strings.TrimSpace(path) == "" {
				continue
			}
			if filepath.Base(path) == scanned.BlockDevice {
				return scanned
			}
		}
	}

	return nil
}

// getScmUsage will retrieve usage statistics (how much space is available for
// new DAOS pools) for either PMem namespaces or SCM emulation with ramdisk.
//
// Usage is only retrieved for active mountpoints being used by online DAOS I/O
// Server instances.
func (c *ControlService) getScmUsage(ssr *storage.ScmScanResponse) (*storage.ScmScanResponse, error) {
	instances := c.harness.Instances()

	nss := make(storage.ScmNamespaces, len(instances))
	for idx, srv := range instances {
		if !srv.isReady() {
			continue // skip if not running
		}

		cfg, err := srv.storage.GetScmConfig()
		if err != nil {
			return nil, err
		}

		mount, err := srv.storage.GetScmUsage()
		if err != nil {
			return nil, err
		}

		switch mount.Class {
		case storage.ClassRAM: // generate fake namespace for emulated ramdisk mounts
			nss[idx] = &storage.ScmNamespace{
				Mount:       mount,
				BlockDevice: "ramdisk",
				Size:        uint64(humanize.GiByte * cfg.Scm.RamdiskSize),
			}
		case storage.ClassDCPM: // update namespace mount info for online storage
			ns := findPMemInScan(ssr, mount.DeviceList)
			if ns == nil {
				return nil, errors.Errorf("instance %d: no pmem namespace for mount %s",
					srv.Index(), mount.Path)
			}
			ns.Mount = mount
			nss[idx] = ns
		default:
			return nil, errors.Errorf("instance %d: unsupported scm class %q",
				srv.Index(), mount.Class)
		}

		c.log.Debugf("updated scm fs usage on device %s mounted at %s: %+v",
			nss[idx].BlockDevice, mount.Path, nss[idx].Mount)
	}

	return &storage.ScmScanResponse{Namespaces: nss}, nil
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
	for _, srv := range instances {
		formatting++
		go func(s *EngineInstance) {
			scmChan <- s.StorageFormatSCM(ctx, req.Reformat)
		}(srv)
	}

	instanceErrored := make(map[uint32]bool)
	for formatting > 0 {
		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		case scmResult := <-scmChan:
			formatting--
			if scmResult.GetState().GetStatus() != ctlpb.ResponseStatus_CTL_SUCCESS {
				instanceErrored[scmResult.GetInstanceidx()] = true
			}
			resp.Mrets = append(resp.Mrets, scmResult)
		}
	}

	// TODO: perform bdev format in parallel
	for _, srv := range instances {
		if !srv.storage.HasBlockDevices() {
			continue
		}

		if instanceErrored[srv.Index()] {
			// if scm errored, indicate skipping bdev format
			if srv.storage.HasBlockDevices() {
				ret := srv.newCret("", nil)
				ret.State.Info = fmt.Sprintf(msgNvmeFormatSkip, srv.Index())
				resp.Crets = append(resp.Crets, ret)
			}
			continue
		}
		// SCM formatted correctly on this instance, format NVMe
		cResults := srv.StorageFormatNVMe()
		if cResults.HasErrors() {
			instanceErrored[srv.Index()] = true
		}
		resp.Crets = append(resp.Crets, cResults...)
	}

	// Notify storage ready for instances formatted without error.
	// Block until all instances have formatted NVMe to avoid
	// VFIO device or resource busy when starting I/O Engines
	// because devices have already been claimed during format.
	// TODO: supply whitelist of instance.Devs to init() on format.
	for _, srv := range instances {
		if instanceErrored[srv.Index()] {
			srv.log.Errorf(msgFormatErr, srv.Index())
			continue
		}
		srv.NotifyStorageReady()
	}

	return resp, nil
}
