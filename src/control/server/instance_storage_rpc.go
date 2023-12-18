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

func smdGetHealth(ctx context.Context, ei *EngineInstance, dev *ctlpb.SmdDevice) error {
	state := dev.Ctrlr.DevState
	if state != ctlpb.NvmeDevState_NORMAL && state != ctlpb.NvmeDevState_EVICTED {
		ei.log.Debugf("skip fetching health stats on device %q in %q state", dev,
			ctlpb.NvmeDevState_name[int32(state)])
		return nil
	}

	health, err := ei.GetBioHealth(ctx, &ctlpb.BioHealthReq{DevUuid: dev.Uuid})
	if err != nil {
		return errors.Wrapf(err, "device %q, state %q", dev, state)
	}
	dev.Ctrlr.HealthStats = health

	return nil
}

func smdQueryEngine(ctx context.Context, engine Engine, pbReq *ctlpb.SmdQueryReq) (*ctlpb.SmdQueryResp_RankResp, error) {
	ei, ok := engine.(*EngineInstance)
	if !ok {
		return nil, errors.New("not EngineInstance")
	}

	engineRank, err := ei.GetRank()
	if err != nil {
		return nil, errors.Wrapf(err, "instance %d GetRank", ei.Index())
	}

	rResp := new(ctlpb.SmdQueryResp_RankResp)
	rResp.Rank = engineRank.Uint32()

	listDevsResp, err := ei.ListSmdDevices(ctx, new(ctlpb.SmdDevReq))
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
		}
	}

	found := false
	for _, dev := range rResp.Devices {
		if pbReq.Uuid != "" && dev.Uuid != pbReq.Uuid {
			continue // Skip health query if UUID doesn't match requested.
		}
		if pbReq.IncludeBioHealth {
			if err := smdGetHealth(ctx, ei, dev); err != nil {
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
