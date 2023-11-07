//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"
	"time"

	"github.com/pkg/errors"
	"golang.org/x/sys/unix"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common/proto"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/server/storage"
)

var (
	scanSmd        = listSmdDevices
	getCtrlrHealth = getBioHealth
)

// newMntRet creates and populates SCM mount result.
// Currently only used for format operations.
func (ei *EngineInstance) newMntRet(mountPoint string, inErr error) *ctlpb.ScmMountResult {
	var info string
	if fault.HasResolution(inErr) {
		info = fault.ShowResolutionFor(inErr)
	}
	return &ctlpb.ScmMountResult{
		Mntpoint:    mountPoint,
		State:       newResponseState(inErr, ctlpb.ResponseStatus_CTL_ERR_SCM, info),
		Instanceidx: ei.Index(),
	}
}

// newCret creates and populates NVMe controller result and logs error
func (ei *EngineInstance) newCret(pciAddr string, inErr error) *ctlpb.NvmeControllerResult {
	var info string
	if pciAddr == "" {
		pciAddr = storage.NilBdevAddress
	}
	if inErr != nil && fault.HasResolution(inErr) {
		info = fault.ShowResolutionFor(inErr)
	}
	return &ctlpb.NvmeControllerResult{
		PciAddr: pciAddr,
		State:   newResponseState(inErr, ctlpb.ResponseStatus_CTL_ERR_NVME, info),
	}
}

func (ei *EngineInstance) logDuration(msg string, start time.Time) {
	ei.log.Infof("%v: %v\n", msg, time.Since(start))
}

// scmFormat will return either successful result or error.
func (ei *EngineInstance) scmFormat(force bool) (*ctlpb.ScmMountResult, error) {
	cfg, err := ei.storage.GetScmConfig()
	if err != nil {
		return nil, err
	}

	defer ei.logDuration(track(fmt.Sprintf(
		"Format of SCM storage for %s instance %d (reformat: %t)", build.DataPlaneName,
		ei.Index(), force)))

	err = ei.storage.FormatScm(force)
	if err != nil {
		return nil, err
	}

	return ei.newMntRet(cfg.Scm.MountPoint, nil), nil
}

func (ei *EngineInstance) bdevFormat() (results proto.NvmeControllerResults) {
	defer ei.logDuration(track(fmt.Sprintf(
		"Format of NVMe storage for %s instance %d", build.DataPlaneName, ei.Index())))

	for _, tr := range ei.storage.FormatBdevTiers() {
		if tr.Error != nil {
			results = append(results, ei.newCret(fmt.Sprintf("tier %d", tr.Tier), tr.Error))
			continue
		}
		for devAddr, status := range tr.Result.DeviceResponses {
			ei.log.Debugf("instance %d: tier %d: device fmt of %s, status %+v",
				ei.Index(), tr.Tier, devAddr, status)

			// TODO DAOS-5828: passing status.Error directly triggers segfault
			var err error
			if status.Error != nil {
				err = status.Error
			}
			results = append(results, ei.newCret(devAddr, err))
		}
	}

	return
}

// StorageFormatSCM performs format on SCM and identifies if superblock needs
// writing.
func (ei *EngineInstance) StorageFormatSCM(ctx context.Context, force bool) (mResult *ctlpb.ScmMountResult) {
	engineIdx := ei.Index()

	var scmErr error
	cfg, err := ei.storage.GetScmConfig()
	if err != nil {
		scmErr = err
		cfg = &storage.TierConfig{Scm: storage.ScmConfig{MountPoint: "unknown"}}
		return
	}

	defer func() {
		if scmErr != nil {
			ei.log.Errorf(msgFormatErr, engineIdx)
			mResult = ei.newMntRet(cfg.Scm.MountPoint, scmErr)
		}
	}()

	if ei.IsStarted() {
		if !force {
			scmErr = errors.Errorf("instance %d: can't format storage of running instance",
				engineIdx)
			return
		}

		ei.log.Infof("forcibly stopping instance %d prior to reformat", ei.Index())
		if scmErr = ei.Stop(unix.SIGKILL); scmErr != nil {
			return
		}

		ei.requestStart(ctx)
	}

	mResult, scmErr = ei.scmFormat(force)
	return
}

// StorageFormatNVMe performs format on NVMe if superblock needs writing.
func (ei *EngineInstance) StorageFormatNVMe() (cResults proto.NvmeControllerResults) {
	// If no superblock exists, format NVMe and populate response with results.
	needsSuperblock, err := ei.NeedsSuperblock()
	if err != nil {
		ei.log.Errorf("engine storage for %s instance %d: NeedsSuperblock(): %s",
			build.DataPlaneName, ei.Index(), err)

		return proto.NvmeControllerResults{
			ei.newCret("", err),
		}
	}

	if needsSuperblock {
		cResults = ei.bdevFormat()
	}

	return
}

func populateCtrlrHealth(ctx context.Context, ei *EngineInstance, smdUuid string, ctrlr *ctlpb.NvmeController) error {
	state := ctrlr.DevState
	if state == ctlpb.NvmeDevState_NEW {
		ei.log.Noticef("skip fetching health stats on device %q in NEW state", ctrlr, state)
		return nil
	}

	health, err := getCtrlrHealth(ctx, ei, &ctlpb.BioHealthReq{DevUuid: smdUuid})
	if err != nil {
		return errors.Wrapf(err, "retrieve health stats for %q (state %q)", ctrlr, state)
	}
	ctrlr.HealthStats = health

	return nil
}

// Scan SMD devices over dRPC and reconstruct NVMe scan response from results.
func scanEngineBdevsOverDrpc(ctx context.Context, ei *EngineInstance, pbReq *ctlpb.ScanNvmeReq) (*ctlpb.ScanNvmeResp, error) {
	scanSmdResp, err := scanSmd(ctx, ei, &ctlpb.SmdDevReq{})
	if err != nil {
		return nil, errors.Wrap(err, "scan smd")
	}

	// Re-link SMD devices inside NVMe controller structures and populate scan response.

	pbResp := ctlpb.ScanNvmeResp{
		State: new(ctlpb.ResponseState),
	}
	seenCtrlrs := make(map[string]*ctlpb.NvmeController)

	for _, sd := range scanSmdResp.Devices {
		if sd.Ctrlr == nil {
			return nil, errors.Errorf("smd %q has no ctrlr ref", sd.Uuid)
		}

		addr := sd.Ctrlr.PciAddr

		if _, exists := seenCtrlrs[addr]; !exists {
			c := new(ctlpb.NvmeController)
			*c = *sd.Ctrlr
			c.SmdDevices = nil
			c.HealthStats = nil
			seenCtrlrs[addr] = c
			pbResp.Ctrlrs = append(pbResp.Ctrlrs, c)
		}

		c := seenCtrlrs[addr]

		// Populate SMD (nvme-meta) if requested.
		if pbReq.Meta {
			nsd := new(ctlpb.SmdDevice)
			*nsd = *sd
			nsd.Ctrlr = nil
			c.SmdDevices = append(c.SmdDevices, nsd)
		}

		// Populate health if requested.
		if pbReq.Health && c.HealthStats == nil {
			if err := populateCtrlrHealth(ctx, ei, sd.Uuid, c); err != nil {
				return nil, err
			}
		}

		// TODO: handle Basic request parameter.
	}
	ei.log.Debugf("seen ctrlrs: %+v", seenCtrlrs)

	return &pbResp, nil
}

// bdevScanEngine calls either in to the private engine storage provider to scan bdevs if engine process
// is not started, otherwise dRPC is used to retrieve details from the online engine.
func bdevScanEngine(ctx context.Context, engine Engine, pbReq *ctlpb.ScanNvmeReq) (*ctlpb.ScanNvmeResp, error) {
	ei, ok := engine.(*EngineInstance)
	if !ok {
		return nil, errors.New("not EngineInstance")
	}

	if pbReq == nil {
		return nil, errors.New("nil request")
	}

	isUp := ei.IsStarted()
	upDn := "down"
	if isUp {
		upDn = "up"
	}
	ei.log.Debugf("scanning engine-%d bdev tiers while engine is %s", ei.Index(), upDn)

	if isUp {
		return scanEngineBdevsOverDrpc(ctx, ei, pbReq)
	}

	// TODO: should anything be passed from pbReq here e.g. Meta/Health specifiers?

	// Retrieve engine cfg bdevs to restrict scan scope.
	req := storage.BdevScanRequest{
		DeviceList: ei.runner.GetConfig().Storage.GetBdevs(),
	}
	if req.DeviceList.Len() == 0 {
		return nil, errors.Errorf("empty device list for engine instance %d", ei.Index())
	}

	return bdevScanToProtoResp(ei.storage.ScanBdevs, req)
}

func smdQueryEngine(ctx context.Context, engine Engine, pbReq *ctlpb.SmdQueryReq) (*ctlpb.SmdQueryResp_RankResp, error) {
	ei, ok := engine.(*EngineInstance)
	if !ok {
		return nil, errors.New("not EngineInstance")
	}

	if pbReq == nil {
		return nil, errors.New("nil request")
	}

	engineRank, err := ei.GetRank()
	if err != nil {
		return nil, errors.Wrapf(err, "instance %d GetRank", ei.Index())
	}
	if !queryRank(pbReq.GetRank(), engineRank) {
		ei.log.Debugf("skipping rank %d not specified in request", engineRank)
		return nil, nil
	}

	rResp := new(ctlpb.SmdQueryResp_RankResp)
	rResp.Rank = engineRank.Uint32()

	if !ei.IsReady() {
		ei.log.Debugf("skipping not-ready instance %d", ei.Index())
		return rResp, nil
	}

	listDevsResp, err := listSmdDevices(ctx, ei, new(ctlpb.SmdDevReq))
	if err != nil {
		return nil, errors.Wrapf(err, "rank %d", engineRank)
	}

	if len(listDevsResp.Devices) == 0 {
		rResp.Devices = nil
		return rResp, nil
	}

	// For each SmdDevice returned in list devs response, append a SmdDeviceWithHealth.
	for _, sd := range listDevsResp.Devices {
		if sd != nil {
			rResp.Devices = append(rResp.Devices, sd)
			//&ctlpb.SmdQueryResp_SmdDeviceWithHealth{Details: sd})
		}
	}

	found := false
	for _, dev := range rResp.Devices {
		if pbReq.Uuid != "" && dev.Uuid != pbReq.Uuid {
			continue // Skip health query if UUID doesn't match requested.
		}
		if pbReq.IncludeBioHealth {
			if err := populateCtrlrHealth(ctx, ei, dev.Uuid, dev.Ctrlr); err != nil {
				return nil, err
			}
		}
		if pbReq.Uuid != "" && dev.Uuid == pbReq.Uuid {
			rResp.Devices = []*ctlpb.SmdDevice{dev}
			found = true
			break
		}
	}
	if pbReq.Uuid != "" && !found {
		rResp.Devices = nil
	}

	return rResp, nil
}
