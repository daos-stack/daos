//
// (C) Copyright 2020 Intel Corporation.
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

	"github.com/daos-stack/daos/src/control/common/proto"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

// newMntRet creates and populates SCM mount result.
// Currently only used for format operations.
func (srv *IOServerInstance) newMntRet(status ctlpb.ResponseStatus, errMsg, infoMsg string) *ctlpb.ScmMountResult {
	return &ctlpb.ScmMountResult{
		Mntpoint:    srv.scmConfig().MountPoint,
		State:       newState(srv.log, status, errMsg, infoMsg, "scm mount format"),
		Instanceidx: srv.Index(),
	}
}

// newCret creates and populates NVMe controller result and logs error
func (srv *IOServerInstance) newCret(pciAddr string, status ctlpb.ResponseStatus, errMsg, infoMsg string) *ctlpb.NvmeControllerResult {
	if pciAddr == "" {
		pciAddr = "<nil>"
	}
	return &ctlpb.NvmeControllerResult{
		Pciaddr: pciAddr,
		State:   newState(srv.log, status, errMsg, infoMsg, "nvme controller format"),
	}
}

// scmFormat will return either successful result or error.
func (srv *IOServerInstance) scmFormat(reformat bool) (*ctlpb.ScmMountResult, error) {
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
	res, err := srv.scmProvider.Format(*req)
	if err == nil && !res.Formatted {
		err = errors.WithMessage(scm.FaultUnknown, "is still unformatted")
	}

	if err != nil {
		srv.log.Errorf("  format of %s failed: %s", scmStr, err)
		return nil, err
	}
	srv.log.Infof("Instance %d: finished format of %s", srvIdx, scmStr)

	return srv.newMntRet(status, eMsg, iMsg), nil
}

func (srv *IOServerInstance) bdevFormat(p *bdev.Provider) (results proto.NvmeControllerResults) {
	srvIdx := srv.Index()
	cfg := srv.bdevConfig()
	results = make(proto.NvmeControllerResults, 0, len(cfg.DeviceList))

	// A config with SCM and no block devices is valid.
	if len(cfg.DeviceList) == 0 {
		return
	}

	srv.log.Infof("Instance %d: starting format of %s block devices %v",
		srvIdx, cfg.Class, cfg.DeviceList)

	res, err := p.Format(bdev.FormatRequest{
		Class:      cfg.Class,
		DeviceList: cfg.DeviceList,
		MemSize:    cfg.MemSize,
	})
	if err != nil {
		results = append(results,
			srv.newCret("", ctlpb.ResponseStatus_CTL_ERR_NVME,
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
			srv.newCret(dev, ctlpbStatus, errMsg, infoMsg))
	}

	srv.log.Infof("Instance %d: finished format of %s block devices %v",
		srvIdx, cfg.Class, cfg.DeviceList)

	return
}

// StorageFormatSCM performs format on SCM and identifies if superblock needs
// writing.
func (srv *IOServerInstance) StorageFormatSCM(reformat bool) (mResult *ctlpb.ScmMountResult) {
	srvIdx := srv.Index()
	needsScmFormat := reformat

	srv.log.Infof("Formatting scm storage for %s instance %d (reformat: %t)",
		DataPlaneName, srvIdx, reformat)

	var scmErr error
	defer func() {
		if scmErr != nil {
			srv.log.Errorf(msgFormatErr, srvIdx)
			mResult = srv.newMntRet(ctlpb.ResponseStatus_CTL_ERR_SCM,
				scmErr.Error(), fault.ShowResolutionFor(scmErr))
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

	if needsScmFormat {
		mResult, scmErr = srv.scmFormat(true)
	}

	return
}

func (srv *IOServerInstance) StorageFormatNVMe(bdevProvider *bdev.Provider) (cResults proto.NvmeControllerResults) {
	srv.log.Infof("Formatting nvme storage for %s instance %d", DataPlaneName, srv.Index())

	// If no superblock exists, format NVMe and populate response with results.
	needsSuperblock, err := srv.NeedsSuperblock()
	if err != nil {
		return proto.NvmeControllerResults{
			srv.newCret("", ctlpb.ResponseStatus_CTL_ERR_NVME,
				err.Error(), fault.ShowResolutionFor(err)),
		}
	}

	if needsSuperblock {
		cResults = srv.bdevFormat(bdevProvider)
	}

	return
}
