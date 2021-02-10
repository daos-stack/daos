//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"fmt"
	"path/filepath"
	"strings"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"
	"golang.org/x/net/context"

	"github.com/daos-stack/daos/src/control/common/proto"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
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

// doNvmePrepare issues prepare request and returns response.
func (c *StorageControlService) doNvmePrepare(req *ctlpb.PrepareNvmeReq) *ctlpb.PrepareNvmeResp {
	_, err := c.NvmePrepare(bdev.PrepareRequest{
		HugePageCount: int(req.GetNrhugepages()),
		TargetUser:    req.GetTargetuser(),
		PCIWhitelist:  req.GetPciwhitelist(),
		ResetOnly:     req.GetReset_(),
	})

	pnr := new(ctlpb.PrepareNvmeResp)
	pnr.State = newResponseState(err, ctlpb.ResponseStatus_CTL_ERR_NVME, "")

	return pnr
}

// newPrepareScmResp sets protobuf SCM prepare response with results.
func newPrepareScmResp(inResp *scm.PrepareResponse, inErr error) (*ctlpb.PrepareScmResp, error) {
	outResp := new(ctlpb.PrepareScmResp)
	outResp.State = new(ctlpb.ResponseState)

	if inErr != nil {
		outResp.State = newResponseState(inErr, ctlpb.ResponseStatus_CTL_ERR_SCM, "")
		return outResp, nil
	}

	if inResp.RebootRequired {
		outResp.Rebootrequired = true
		outResp.State.Info = scm.MsgRebootRequired
	}

	outResp.Namespaces = make(proto.ScmNamespaces, 0, len(inResp.Namespaces))
	if err := (*proto.ScmNamespaces)(&outResp.Namespaces).FromNative(inResp.Namespaces); err != nil {
		return nil, err
	}

	return outResp, nil
}

func (c *StorageControlService) doScmPrepare(pbReq *ctlpb.PrepareScmReq) (*ctlpb.PrepareScmResp, error) {
	scmState, err := c.GetScmState()
	if err != nil {
		return newPrepareScmResp(nil, err)
	}
	c.log.Debugf("SCM state before prep: %s", scmState)

	resp, err := c.ScmPrepare(scm.PrepareRequest{Reset: pbReq.Reset_})

	return newPrepareScmResp(resp, err)
}

// StoragePrepare configures SSDs for user specific access with SPDK and
// groups SCM modules in AppDirect/interleaved mode as kernel "pmem" devices.
func (c *StorageControlService) StoragePrepare(ctx context.Context, req *ctlpb.StoragePrepareReq) (*ctlpb.StoragePrepareResp, error) {
	c.log.Debug("received StoragePrepare RPC; proceeding to instance storage preparation")

	resp := new(ctlpb.StoragePrepareResp)

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
// health statistics and stored server meta-data. If I/O servers are running
// then query is issued over dRPC as go-spdk bindings cannot be used to access
// controller claimed by another process. Only update info for controllers
// assigned to I/O servers.
func (c *ControlService) scanInstanceBdevs(ctx context.Context) (*bdev.ScanResponse, error) {
	var ctrlrs storage.NvmeControllers
	instances := c.harness.Instances()

	for _, srv := range instances {
		nvmeDevs := c.instanceStorage[srv.Index()].Bdev.GetNvmeDevs()
		if len(nvmeDevs) == 0 {
			continue
		}

		// only retrieve results for devices listed in server config
		bdevReq := bdev.ScanRequest{DeviceList: nvmeDevs}

		c.log.Debugf("instance %d storage scan: only show bdev devices in config %v",
			srv.Index(), bdevReq.DeviceList)

		// scan through control-plane to get up-to-date stats if io
		// server is not active (and therefore has not claimed the
		// assigned devices), bypass cache to get fresh health stats
		if !srv.isReady() {
			bdevReq.NoCache = true

			bsr, err := c.NvmeScan(bdevReq)
			if err != nil {
				return nil, errors.Wrap(err, "nvme scan")
			}

			ctrlrs = ctrlrs.Update(bsr.Controllers...)
			continue
		}

		bsr, err := c.NvmeScan(bdevReq)
		if err != nil {
			return nil, errors.Wrap(err, "nvme scan")
		}

		ctrlrMap, err := mapCtrlrs(bsr.Controllers)
		if err != nil {
			return nil, errors.Wrap(err, "create controller map")
		}

		// if io servers are active and have claimed the assigned devices,
		// query over drpc to update controller details with current health
		// stats and smd info
		if err := srv.updateInUseBdevs(ctx, ctrlrMap); err != nil {
			return nil, errors.Wrap(err, "updating bdev health and smd info")
		}

		ctrlrs = ctrlrs.Update(bsr.Controllers...)
	}

	return &bdev.ScanResponse{Controllers: ctrlrs}, nil
}

// stripNvmeDetails removes all controller details leaving only PCI address and
// NUMA node/socket ID. Useful when scanning only device topology.
func stripNvmeDetails(pbc *ctlpb.NvmeController) {
	pbc.Serial = ""
	pbc.Model = ""
	pbc.Fwrev = ""
	pbc.Namespaces = nil
}

// newScanBdevResp populates protobuf NVMe scan response with controller info
// including health statistics or metadata if requested.
func newScanNvmeResp(req *ctlpb.ScanNvmeReq, inResp *bdev.ScanResponse, inErr error) (*ctlpb.ScanNvmeResp, error) {
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
			pbc.Healthstats = nil
		}
		if !req.GetMeta() {
			pbc.Smddevices = nil
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
	resp, err := c.NvmeScan(bdev.ScanRequest{})

	return newScanNvmeResp(req, resp, err)
}

// newScanScmResp sets protobuf SCM scan response with module or namespace info.
func newScanScmResp(inResp *scm.ScanResponse, inErr error) (*ctlpb.ScanScmResp, error) {
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

func findPMemInScan(ssr *scm.ScanResponse, cfg *storage.ScmConfig) *storage.ScmNamespace {
	for _, scanned := range ssr.Namespaces {
		for _, path := range cfg.DeviceList {
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
func (c *ControlService) getScmUsage(ssr *scm.ScanResponse) (*scm.ScanResponse, error) {
	instances := c.harness.Instances()

	nss := make(storage.ScmNamespaces, len(instances))
	for idx, srv := range instances {
		if !srv.isReady() {
			continue // skip if not running
		}

		cfg := srv.scmConfig()

		mount, err := srv.scmProvider.GetfsUsage(cfg.MountPoint)
		if err != nil {
			return nil, err
		}

		switch cfg.Class {
		case storage.ScmClassRAM: // generate fake namespace for emulated ramdisk mounts
			nss[idx] = &storage.ScmNamespace{
				Mount:       mount,
				BlockDevice: "ramdisk",
				Size:        uint64(humanize.GiByte * cfg.RamdiskSize),
			}
		case storage.ScmClassDCPM: // update namespace mount info for online storage
			ns := findPMemInScan(ssr, &cfg)
			if ns == nil {
				return nil, errors.Errorf("instance %d: no pmem namespace for mount %s",
					srv.Index(), cfg.MountPoint)
			}
			ns.Mount = mount
			nss[idx] = ns
		default:
			return nil, errors.Errorf("instance %d: unsupported scm class %q",
				srv.Index(), cfg.Class)
		}

		c.log.Debugf("updated scm fs usage on device %s mounted at %s: %+v",
			nss[idx].BlockDevice, cfg.MountPoint, nss[idx].Mount)
	}

	return &scm.ScanResponse{Namespaces: nss}, nil
}

// scanScm will return mount details and usage for either emulated RAM or real PMem.
func (c *ControlService) scanScm(ctx context.Context, req *ctlpb.ScanScmReq) (*ctlpb.ScanScmResp, error) {
	if req == nil {
		return nil, errors.New("nil scm request")
	}

	// scan SCM, rescan scm storage details by default
	scmReq := scm.ScanRequest{Rescan: true}
	ssr, scanErr := c.ScmScan(scmReq)

	if scanErr != nil || !req.GetUsage() {
		return newScanScmResp(ssr, scanErr)
	}

	return newScanScmResp(c.getScmUsage(ssr))
}

// StorageScan discovers non-volatile storage hardware on node.
func (c *ControlService) StorageScan(ctx context.Context, req *ctlpb.StorageScanReq) (*ctlpb.StorageScanResp, error) {
	c.log.Debug("received StorageScan RPC")

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

	c.log.Debugf("received StorageFormat RPC %v; proceeding to instance storage format", req)

	// TODO: enable per-instance formatting
	formatting := 0
	for _, srv := range instances {
		formatting++
		go func(s *IOServerInstance) {
			scmChan <- s.StorageFormatSCM(req.Reformat)
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
		if instanceErrored[srv.Index()] {
			// if scm errored, indicate skipping bdev format
			if len(srv.bdevConfig().DeviceList) > 0 {
				ret := srv.newCret("", nil)
				ret.State.Info = fmt.Sprintf(msgNvmeFormatSkip, srv.Index())
				resp.Crets = append(resp.Crets, ret)
			}
			continue
		}
		// SCM formatted correctly on this instance, format NVMe
		cResults := srv.StorageFormatNVMe(c.bdev)
		if cResults.HasErrors() {
			instanceErrored[srv.Index()] = true
		}
		resp.Crets = append(resp.Crets, cResults...)
	}

	// Notify storage ready for instances formatted without error.
	// Block until all instances have formatted NVMe to avoid
	// VFIO device or resource busy when starting IO servers
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
