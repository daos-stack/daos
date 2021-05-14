//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"

	"github.com/pkg/errors"
	"golang.org/x/sys/unix"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common/proto"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

// newMntRet creates and populates SCM mount result.
// Currently only used for format operations.
func (ei *EngineInstance) newMntRet(inErr error) *ctlpb.ScmMountResult {
	var info string
	if fault.HasResolution(inErr) {
		info = fault.ShowResolutionFor(inErr)
	}
	return &ctlpb.ScmMountResult{
		Mntpoint:    ei.scmConfig().MountPoint,
		State:       newResponseState(inErr, ctlpb.ResponseStatus_CTL_ERR_SCM, info),
		Instanceidx: ei.Index(),
	}
}

// newCret creates and populates NVMe controller result and logs error
func (ei *EngineInstance) newCret(pciAddr string, inErr error) *ctlpb.NvmeControllerResult {
	var info string
	if pciAddr == "" {
		pciAddr = "<nil>"
	}
	if inErr != nil && fault.HasResolution(inErr) {
		info = fault.ShowResolutionFor(inErr)
	}
	return &ctlpb.NvmeControllerResult{
		PciAddr: pciAddr,
		State:   newResponseState(inErr, ctlpb.ResponseStatus_CTL_ERR_NVME, info),
	}
}

// scmFormat will return either successful result or error.
func (ei *EngineInstance) scmFormat(reformat bool) (*ctlpb.ScmMountResult, error) {
	engineIdx := ei.Index()
	cfg := ei.scmConfig()

	req, err := scm.CreateFormatRequest(cfg, reformat)
	if err != nil {
		return nil, errors.Wrap(err, "generate format request")
	}

	scmStr := fmt.Sprintf("SCM (%s:%s)", cfg.Class, cfg.MountPoint)
	ei.log.Infof("Instance %d: starting format of %s", engineIdx, scmStr)
	res, err := ei.scmProvider.Format(*req)
	if err == nil && !res.Formatted {
		err = errors.WithMessage(scm.FaultUnknown, "is still unformatted")
	}

	if err != nil {
		ei.log.Errorf("  format of %s failed: %s", scmStr, err)
		return nil, err
	}
	ei.log.Infof("Instance %d: finished format of %s", engineIdx, scmStr)

	return ei.newMntRet(nil), nil
}

func (ei *EngineInstance) bdevFormat(p *bdev.Provider) (results proto.NvmeControllerResults) {
	engineIdx := ei.Index()
	cfg := ei.bdevConfig()
	results = make(proto.NvmeControllerResults, 0, len(cfg.DeviceList))

	// A config with SCM and no block devices is valid.
	if len(cfg.DeviceList) == 0 {
		return
	}

	ei.log.Infof("Instance %d: starting format of %s block devices %v",
		engineIdx, cfg.Class, cfg.DeviceList)

	res, err := p.Format(bdev.FormatRequest{
		Class:      cfg.Class,
		DeviceList: cfg.DeviceList,
		MemSize:    cfg.MemSize,
	})
	if err != nil {
		results = append(results, ei.newCret("", err))
		return
	}

	for dev, status := range res.DeviceResponses {
		// TODO DAOS-5828: passing status.Error directly triggers segfault
		var err error
		if status.Error != nil {
			err = status.Error
		}
		results = append(results, ei.newCret(dev, err))
	}

	ei.log.Infof("Instance %d: finished format of %s block devices %v",
		engineIdx, cfg.Class, cfg.DeviceList)

	return
}

// StorageFormatSCM performs format on SCM and identifies if superblock needs
// writing.
func (ei *EngineInstance) StorageFormatSCM(ctx context.Context, reformat bool) (mResult *ctlpb.ScmMountResult) {
	engineIdx := ei.Index()
	needsScmFormat := reformat

	ei.log.Infof("Formatting scm storage for %s instance %d (reformat: %t)",
		build.DataPlaneName, engineIdx, reformat)

	var scmErr error
	defer func() {
		if scmErr != nil {
			ei.log.Errorf(msgFormatErr, engineIdx)
			mResult = ei.newMntRet(scmErr)
		}
	}()

	if ei.isStarted() {
		if !reformat {
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

	// If not reformatting, check if SCM is already formatted.
	if !reformat {
		needsScmFormat, scmErr = ei.NeedsScmFormat()
		if scmErr == nil && !needsScmFormat {
			scmErr = scm.FaultFormatNoReformat
		}
		if scmErr != nil {
			return
		}
	}

	if needsScmFormat {
		mResult, scmErr = ei.scmFormat(true)
	}

	return
}

// StorageFormatNVMe performs format on NVMe if superblock needs writing.
func (ei *EngineInstance) StorageFormatNVMe(bdevProvider *bdev.Provider) (cResults proto.NvmeControllerResults) {
	ei.log.Infof("Formatting nvme storage for %s instance %d", build.DataPlaneName, ei.Index())

	// If no superblock exists, format NVMe and populate response with results.
	needsSuperblock, err := ei.NeedsSuperblock()
	if err != nil {
		return proto.NvmeControllerResults{
			ei.newCret("", err),
		}
	}

	if needsSuperblock {
		cResults = ei.bdevFormat(bdevProvider)
	}

	return
}
