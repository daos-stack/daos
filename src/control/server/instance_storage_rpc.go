//
// (C) Copyright 2020-2024 Intel Corporation.
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
	scanSmd                       = listSmdDevices
	getCtrlrHealth                = getBioHealth
	errEngineBdevScanEmptyDevList = errors.New("empty device list for engine instance")
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

func formatEngineBdevs(ei *EngineInstance, ctrlrs storage.NvmeControllers) (results proto.NvmeControllerResults) {
	// If no superblock exists, format NVMe and populate response with results.
	needsSuperblock, err := ei.NeedsSuperblock()
	if err != nil {
		ei.log.Errorf("engine storage for %s instance %d: NeedsSuperblock(): %s",
			build.DataPlaneName, ei.Index(), err)

		return proto.NvmeControllerResults{
			ei.newCret("", err),
		}
	}

	if !needsSuperblock {
		return
	}

	defer ei.logDuration(track(fmt.Sprintf(
		"Format of NVMe storage for %s instance %d", build.DataPlaneName, ei.Index())))

	for _, tr := range ei.storage.FormatBdevTiers(ctrlrs) {
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

func populateCtrlrHealth(ctx context.Context, engine Engine, req *ctlpb.BioHealthReq, ctrlr *ctlpb.NvmeController) (bool, error) {
	stateName := ctlpb.NvmeDevState_name[int32(ctrlr.DevState)]
	if !ctrlr.CanSupplyHealthStats() {
		engine.Debugf("skip fetching health stats on device %q in %q state",
			ctrlr.PciAddr, stateName)
		return false, nil
	}

	health, err := getCtrlrHealth(ctx, engine, req)
	if err != nil {
		return false, errors.Wrapf(err, "retrieve health stats for %q (state %q)", ctrlr,
			stateName)
	}
	ctrlr.HealthStats = health

	return true, nil
}

// Scan SMD devices over dRPC and reconstruct NVMe scan response from results.
func scanEngineBdevsOverDrpc(ctx context.Context, engine Engine, pbReq *ctlpb.ScanNvmeReq) (*ctlpb.ScanNvmeResp, error) {
	scanSmdResp, err := scanSmd(ctx, engine, &ctlpb.SmdDevReq{})
	if err != nil {
		return nil, errors.Wrap(err, "scan smd")
	}
	if scanSmdResp == nil {
		return nil, errors.New("nil smd scan resp")
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

		if !sd.Ctrlr.IsScannable() {
			engine.Debugf("smd %q skip ctrlr %+v with bad state", sd.Uuid, sd.Ctrlr)
			continue
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

		// Populate health if requested.
		healthUpdated := false
		if pbReq.Health && c.HealthStats == nil {
			bhReq := &ctlpb.BioHealthReq{
				DevUuid:  sd.Uuid,
				MetaSize: pbReq.MetaSize,
				RdbSize:  pbReq.RdbSize,
			}
			upd, err := populateCtrlrHealth(ctx, engine, bhReq, c)
			if err != nil {
				return nil, err
			}
			healthUpdated = upd
		}

		// Populate SMD (meta) if requested.
		if pbReq.Meta {
			nsd := new(ctlpb.SmdDevice)
			*nsd = *sd
			nsd.Ctrlr = nil
			nsd.MetaSize = pbReq.MetaSize
			nsd.RdbSize = pbReq.RdbSize
			if healthUpdated {
				// Populate space usage for each SMD device from health stats.
				nsd.TotalBytes = c.HealthStats.TotalBytes
				nsd.AvailBytes = c.HealthStats.AvailBytes
				nsd.ClusterSize = c.HealthStats.ClusterSize
				nsd.MetaWalSize = c.HealthStats.MetaWalSize
				nsd.RdbWalSize = c.HealthStats.RdbWalSize
			}
			engineRank, err := engine.GetRank()
			if err != nil {
				return nil, errors.Wrapf(err, "instance %d GetRank", engine.Index())
			}
			nsd.Rank = engineRank.Uint32()
			c.SmdDevices = append(c.SmdDevices, nsd)
		}
	}

	return &pbResp, nil
}

func bdevScanEngineAssigned(ctx context.Context, engine Engine, pbReq *ctlpb.ScanNvmeReq, devList *storage.BdevDeviceList, isStarted *bool) (*ctlpb.ScanNvmeResp, error) {
	*isStarted = engine.IsStarted()
	if !*isStarted {
		engine.Debugf("scanning engine-%d bdev tiers while engine is down", engine.Index())

		// Retrieve engine cfg bdevs to restrict scan scope.
		req := storage.BdevScanRequest{DeviceList: devList}

		return bdevScanToProtoResp(engine.GetStorage().ScanBdevs, req)
	}

	engine.Debugf("scanning engine-%d bdev tiers while engine is up", engine.Index())

	// If engine is started but not ready, wait for ready state. If partial number of engines
	// return results, indicate errors for non-ready engines whilst returning successful scan
	// results.
	pollFn := func(e Engine) bool { return e.IsReady() }
	if err := pollInstanceState(ctx, []Engine{engine}, pollFn); err != nil {
		return nil, errors.Wrapf(err, "waiting for engine %d to be ready to receive drpcs",
			engine.Index())
	}

	return scanEngineBdevsOverDrpc(ctx, engine, pbReq)
}

// bdevScanEngine calls either in to the private engine storage provider to scan bdevs if engine process
// is not started, otherwise dRPC is used to retrieve details from the online engine.
func bdevScanEngine(ctx context.Context, engine Engine, req *ctlpb.ScanNvmeReq) (resp *ctlpb.ScanNvmeResp, err error) {
	if req == nil {
		return nil, errors.New("nil request")
	}

	eCfgBdevs := storage.TierConfigs(engine.GetStorage().GetBdevConfigs()).Bdevs()
	if eCfgBdevs.Len() == 0 {
		return nil, errEngineBdevScanEmptyDevList
	}

	var isStarted bool
	resp, err = bdevScanEngineAssigned(ctx, engine, req, eCfgBdevs, &isStarted)
	if err != nil {
		return nil, err
	}

	// Retry once if engine provider scan returns unexpected number of controllers in case
	// engines claimed devices between when started state was checked and scan was executed.
	if !isStarted && len(resp.Ctrlrs) != eCfgBdevs.Len() {
		engine.Debugf("retrying engine bdev scan as unexpected nr returned, want %d got %d",
			eCfgBdevs.Len(), len(resp.Ctrlrs))

		resp, err = bdevScanEngineAssigned(ctx, engine, req, eCfgBdevs, &isStarted)
		if err != nil {
			return nil, err
		}
	}

	if len(resp.Ctrlrs) != eCfgBdevs.Len() {
		engine.Debugf("engine bdev scan returned unexpected nr, want %d got %d",
			eCfgBdevs.Len(), len(resp.Ctrlrs))
	}

	return
}

func smdQueryEngine(ctx context.Context, engine Engine, pbReq *ctlpb.SmdQueryReq) (*ctlpb.SmdQueryResp_RankResp, error) {
	engineRank, err := engine.GetRank()
	if err != nil {
		return nil, errors.Wrapf(err, "instance %d GetRank", engine.Index())
	}

	rResp := new(ctlpb.SmdQueryResp_RankResp)
	rResp.Rank = engineRank.Uint32()

	listDevsResp, err := listSmdDevices(ctx, engine, new(ctlpb.SmdDevReq))
	if err != nil {
		return nil, errors.Wrapf(err, "rank %d", engineRank)
	}

	if len(listDevsResp.Devices) == 0 {
		rResp.Devices = nil
		return rResp, nil
	}

	for _, sd := range listDevsResp.Devices {
		if sd != nil {
			rResp.Devices = append(rResp.Devices, sd)
		}
	}

	found := false
	for _, dev := range rResp.Devices {
		if pbReq.Uuid != "" && dev.Uuid != pbReq.Uuid {
			continue // Skip health query if UUID doesn't match requested.
		}
		if pbReq.IncludeBioHealth {
			bhReq := &ctlpb.BioHealthReq{DevUuid: dev.Uuid}
			if _, err := populateCtrlrHealth(ctx, engine, bhReq, dev.Ctrlr); err != nil {
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
