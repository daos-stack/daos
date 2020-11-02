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

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common/proto"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

// newMntRet creates and populates SCM mount result.
// Currently only used for format operations.
func (srv *IOServerInstance) newMntRet(inErr error) *ctlpb.ScmMountResult {
	var info string
	if fault.HasResolution(inErr) {
		info = fault.ShowResolutionFor(inErr)
	}
	return &ctlpb.ScmMountResult{
		Mntpoint:    srv.scmConfig().MountPoint,
		State:       newResponseState(inErr, ctlpb.ResponseStatus_CTL_ERR_SCM, info),
		Instanceidx: srv.Index(),
	}
}

// newCret creates and populates NVMe controller result and logs error
func (srv *IOServerInstance) newCret(pciAddr string, inErr error) *ctlpb.NvmeControllerResult {
	var info string
	if pciAddr == "" {
		pciAddr = "<nil>"
	}
	if inErr != nil && fault.HasResolution(inErr) {
		info = fault.ShowResolutionFor(inErr)
	}
	return &ctlpb.NvmeControllerResult{
		Pciaddr: pciAddr,
		State:   newResponseState(inErr, ctlpb.ResponseStatus_CTL_ERR_NVME, info),
	}
}

// scmFormat will return either successful result or error.
func (srv *IOServerInstance) scmFormat(reformat bool) (*ctlpb.ScmMountResult, error) {
	srvIdx := srv.Index()
	cfg := srv.scmConfig()

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

	return srv.newMntRet(nil), nil
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
		results = append(results, srv.newCret("", err))
		return
	}

	for dev, status := range res.DeviceResponses {
		// TODO DAOS-5828: passing status.Error directly triggers segfault
		var err error
		if status.Error != nil {
			err = status.Error
		}
		results = append(results, srv.newCret(dev, err))
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
		build.DataPlaneName, srvIdx, reformat)

	var scmErr error
	defer func() {
		if scmErr != nil {
			srv.log.Errorf(msgFormatErr, srvIdx)
			mResult = srv.newMntRet(scmErr)
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

// StorageFormatNVMe performs format on NVMe if superblock needs writing.
func (srv *IOServerInstance) StorageFormatNVMe(bdevProvider *bdev.Provider) (cResults proto.NvmeControllerResults) {
	srv.log.Infof("Formatting nvme storage for %s instance %d", build.DataPlaneName, srv.Index())

	// If no superblock exists, format NVMe and populate response with results.
	needsSuperblock, err := srv.NeedsSuperblock()
	if err != nil {
		return proto.NvmeControllerResults{
			srv.newCret("", err),
		}
	}

	if needsSuperblock {
		cResults = srv.bdevFormat(bdevProvider)
	}

	return
}
