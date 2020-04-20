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
	"context"
	"os"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

// scmConfig returns the scm configuration assigned to this instance.
func (srv *IOServerInstance) scmConfig() storage.ScmConfig {
	return srv.runner.GetConfig().Storage.SCM
}

// bdevConfig returns the block device configuration assigned to this instance.
func (srv *IOServerInstance) bdevConfig() storage.BdevConfig {
	return srv.runner.GetConfig().Storage.Bdev
}

// MountScmDevice mounts the configured SCM device (DCPM or ramdisk emulation)
// at the mountpoint specified in the configuration. If the device is already
// mounted, the function returns nil, indicating success.
func (srv *IOServerInstance) MountScmDevice() error {
	scmCfg := srv.scmConfig()

	isMount, err := srv.scmProvider.IsMounted(scmCfg.MountPoint)
	if err != nil && !os.IsNotExist(errors.Cause(err)) {
		return errors.WithMessage(err, "failed to check SCM mount")
	}
	if isMount {
		return nil
	}

	srv.log.Debugf("attempting to mount existing SCM dir %s\n", scmCfg.MountPoint)

	var res *scm.MountResponse
	switch scmCfg.Class {
	case storage.ScmClassRAM:
		res, err = srv.scmProvider.MountRamdisk(scmCfg.MountPoint, uint(scmCfg.RamdiskSize))
	case storage.ScmClassDCPM:
		if len(scmCfg.DeviceList) != 1 {
			err = scm.FaultFormatInvalidDeviceCount
			break
		}
		res, err = srv.scmProvider.MountDcpm(scmCfg.DeviceList[0], scmCfg.MountPoint)
	default:
		err = errors.New(scm.MsgScmClassNotSupported)
	}
	if err != nil {
		return errors.WithMessage(err, "mounting existing scm dir")
	}
	srv.log.Debugf("%s mounted: %t", res.Target, res.Mounted)

	return nil
}

// NeedsScmFormat probes the configured instance storage and determines whether
// or not it requires a format operation before it can be used.
func (srv *IOServerInstance) NeedsScmFormat() (bool, error) {
	srv.RLock()
	if srv._scmStorageOk {
		srv.RUnlock()
		return false, nil
	}
	srv.RUnlock()

	scmCfg := srv.scmConfig()

	srv.log.Debugf("%s: checking formatting", scmCfg.MountPoint)

	// take a lock here to ensure that we can safely set _scmStorageOk
	// as well as avoiding racy access to stuff in srv.ext.
	srv.Lock()
	defer srv.Unlock()

	req, err := scm.CreateFormatRequest(scmCfg, false)
	if err != nil {
		return false, err
	}

	res, err := srv.scmProvider.CheckFormat(*req)
	if err != nil {
		return false, err
	}

	needsFormat := !res.Mounted && !res.Mountable
	srv.log.Debugf("%s (%s) needs format: %t", scmCfg.MountPoint, scmCfg.Class, needsFormat)
	return needsFormat, nil
}

// NotifyStorageReady releases any blocks on AwaitStorageReady().
func (srv *IOServerInstance) NotifyStorageReady() {
	go func() {
		close(srv.storageReady)
	}()
}

// AwaitStorageReady blocks until the IOServer's storage is ready.
func (srv *IOServerInstance) AwaitStorageReady(ctx context.Context) {
	select {
	case <-ctx.Done():
		srv.log.Infof("%s instance %d storage not ready: %s", DataPlaneName, srv.Index(), ctx.Err())
	case <-srv.storageReady:
		srv.log.Infof("%s instance %d storage ready", DataPlaneName, srv.Index())
	}
}
