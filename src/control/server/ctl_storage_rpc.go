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
func (c *StorageControlService) StoragePrepare(ctx context.Context, req *ctlpb.StoragePrepareReq) (*ctlpb.StoragePrepareResp, error) {
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

// mapCtrlrs maps controllers to an alternate (as opposed to PCI address) key.
func mapCtrlrs(ctrlrs storage.NvmeControllers) (map[string]*storage.NvmeController, error) {
	ctrlrMap := make(map[string]*storage.NvmeController)

	for _, ctrlr := range ctrlrs {
		key, err := ctrlr.GenAltKey()
		if err != nil {
			return nil, errors.Wrapf(err, "generate alternate key for controller %s",
				ctrlr.PciAddr)
		}

		if _, exists := ctrlrMap[key]; exists {
			return nil, errors.Errorf("duplicate entries for controller %s, key %s",
				ctrlr.PciAddr, key)
		}

		ctrlrMap[key] = ctrlr
	}

	return ctrlrMap, nil
}

// scanInstanceBdevs retrieves up-to-date NVMe controller info including
// health statistics and stored server meta-data. If I/O servers are running
// then query is issued over dRPC as go-spdk bindings cannot be used to access
// controller claimed by another process. Only update info for controllers
// assigned to I/O servers.
func (c *ControlService) scanInstanceBdevs(ctx context.Context) (storage.NvmeControllers, error) {
	var ctrlrs storage.NvmeControllers
	instances := c.harness.Instances()

	for _, srv := range instances {
		bdevReq := bdev.ScanRequest{} // use cached controller details by default

		// only retrieve results for devices listed in server config
		bdevReq.DeviceList = c.instanceStorage[srv.Index()].Bdev.GetNvmeDevs()
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

			ctrlrs = append(ctrlrs, bsr.Controllers...)
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

		ctrlrs = append(ctrlrs, bsr.Controllers...)
	}

	return ctrlrs, nil
}

// setBdevScanResp populates protobuf NVMe scan response with controller info
// including health statistics or metadata if requested.
func (c *ControlService) setBdevScanResp(cs storage.NvmeControllers, inErr error, req *ctlpb.ScanNvmeReq, resp *ctlpb.ScanNvmeResp) error {
	state := newState(c.log, ctlpb.ResponseStatus_CTL_SUCCESS, "", "", "Scan NVMe Storage")

	if inErr != nil {
		state.Status = ctlpb.ResponseStatus_CTL_ERR_NVME
		state.Error = inErr.Error()
		if fault.HasResolution(inErr) {
			state.Info = fault.ShowResolutionFor(inErr)
		}
		resp.State = state

		return nil
	}

	pbCtrlrs := make(proto.NvmeControllers, 0, len(cs))
	if err := pbCtrlrs.FromNative(cs); err != nil {
		return errors.Wrapf(err, "convert %#v to protobuf format", cs)
	}

	// trim unwanted fields so responses can be coalesced from hash map
	for _, pbc := range pbCtrlrs {
		if !req.Health {
			pbc.Healthstats = nil
		}
		if !req.Meta {
			pbc.Smddevices = nil
		}
	}

	resp.State = state
	resp.Ctrlrs = pbCtrlrs

	return nil
}

// scanBdevs updates transient details if health statistics or server metadata
// is requested otherwise just retrieves cached static controller details.
func (c *ControlService) scanBdevs(ctx context.Context, req *ctlpb.ScanNvmeReq, resp *ctlpb.ScanNvmeResp) error {
	if req.Health || req.Meta {
		// filter results based on config file bdev_list contents
		ctrlrs, err := c.scanInstanceBdevs(ctx)

		return c.setBdevScanResp(ctrlrs, err, req, resp)
	}

	// return cached results for all bdevs
	bsr, scanErr := c.NvmeScan(bdev.ScanRequest{})

	return c.setBdevScanResp(bsr.Controllers, errors.Wrap(scanErr, "NvmeScan()"), req, resp)
}

func (c *ControlService) scanInstanceScm(ctx context.Context) (storage.ScmNamespaces, storage.ScmModules, error) {
	// rescan scm storage details by default
	scmReq := scm.ScanRequest{Rescan: true}
	ssr, err := c.ScmScan(scmReq)
	if err != nil {
		return nil, nil, errors.Wrap(err, "ScmScan()")
	}

	return ssr.Namespaces, ssr.Modules, nil
}

func (c *ControlService) scanScm(ctx context.Context, resp *ctlpb.ScanScmResp) error {
	state := newState(c.log, ctlpb.ResponseStatus_CTL_SUCCESS, "", "", "Scan SCM Storage")

	namespaces, modules, err := c.scanInstanceScm(ctx)
	if err != nil {
		state.Status = ctlpb.ResponseStatus_CTL_ERR_SCM
		state.Error = err.Error()
		if fault.HasResolution(err) {
			state.Info = fault.ShowResolutionFor(err)
		}
		resp.State = state

		return nil
	}

	if len(namespaces) > 0 {
		resp.Namespaces = make(proto.ScmNamespaces, 0, len(namespaces))
		err := (*proto.ScmNamespaces)(&resp.Namespaces).FromNative(namespaces)
		if err != nil {
			return errors.Wrapf(err, "convert %#v to protobuf format", namespaces)
		}
		resp.State = state

		return nil
	}

	resp.Modules = make(proto.ScmModules, 0, len(modules))
	if err := (*proto.ScmModules)(&resp.Modules).FromNative(modules); err != nil {
		return errors.Wrapf(err, "convert %#v to protobuf format", modules)
	}
	resp.State = state

	return nil
}

// StorageScan discovers non-volatile storage hardware on node.
func (c *ControlService) StorageScan(ctx context.Context, req *ctlpb.StorageScanReq) (*ctlpb.StorageScanResp, error) {
	c.log.Debug("received StorageScan RPC")

	resp := new(ctlpb.StorageScanResp)
	resp.Nvme = new(ctlpb.ScanNvmeResp)
	resp.Scm = new(ctlpb.ScanScmResp)

	if err := c.scanBdevs(ctx, req.Nvme, resp.Nvme); err != nil {
		return nil, err
	}

	if err := c.scanScm(ctx, resp.Scm); err != nil {
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
				resp.Crets = append(resp.Crets,
					srv.newCret("", ctlpb.ResponseStatus_CTL_SUCCESS, "",
						fmt.Sprintf(msgNvmeFormatSkip, srv.Index())))
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
