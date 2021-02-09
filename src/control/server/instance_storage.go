//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"os"
	"path"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

// scmConfig returns the scm configuration assigned to this instance.
func (srv *EngineInstance) scmConfig() storage.ScmConfig {
	return srv.runner.GetConfig().Storage.SCM
}

// bdevConfig returns the block device configuration assigned to this instance.
func (srv *EngineInstance) bdevConfig() storage.BdevConfig {
	return srv.runner.GetConfig().Storage.Bdev
}

// MountScmDevice mounts the configured SCM device (DCPM or ramdisk emulation)
// at the mountpoint specified in the configuration. If the device is already
// mounted, the function returns nil, indicating success.
func (srv *EngineInstance) MountScmDevice() error {
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
		err = errors.New(scm.MsgClassNotSupported)
	}
	if err != nil {
		return errors.WithMessage(err, "mounting existing scm dir")
	}
	srv.log.Debugf("%s mounted: %t", res.Target, res.Mounted)

	return nil
}

// NeedsScmFormat probes the configured instance storage and determines whether
// or not it requires a format operation before it can be used.
func (srv *EngineInstance) NeedsScmFormat() (bool, error) {
	scmCfg := srv.scmConfig()

	srv.log.Debugf("%s: checking formatting", scmCfg.MountPoint)

	srv.RLock()
	defer srv.RUnlock()

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

// NotifyStorageReady releases any blocks on awaitStorageReady().
func (srv *EngineInstance) NotifyStorageReady() {
	go func() {
		srv.storageReady <- true
	}()
}

// awaitStorageReady blocks until instance has storage available and ready to be used.
func (srv *EngineInstance) awaitStorageReady(ctx context.Context, skipMissingSuperblock bool) error {
	idx := srv.Index()

	if srv.isStarted() {
		return errors.Errorf("can't wait for storage: instance %d already started", idx)
	}

	srv.log.Infof("Checking %s instance %d storage ...", build.DataPlaneName, idx)

	needsScmFormat, err := srv.NeedsScmFormat()
	if err != nil {
		srv.log.Errorf("instance %d: failed to check storage formatting: %s", idx, err)
		needsScmFormat = true
	}

	if !needsScmFormat {
		if skipMissingSuperblock {
			return nil
		}
		srv.log.Debugf("instance %d: no SCM format required; checking for superblock", idx)
		needsSuperblock, err := srv.NeedsSuperblock()
		if err != nil {
			srv.log.Errorf("instance %d: failed to check instance superblock: %s", idx, err)
		}
		if !needsSuperblock {
			srv.log.Debugf("instance %d: superblock not needed", idx)
			return nil
		}
	}

	if skipMissingSuperblock {
		return FaultScmUnmanaged(srv.scmConfig().MountPoint)
	}

	// by this point we need superblock and possibly scm format
	formatType := "SCM"
	if !needsScmFormat {
		formatType = "Metadata"
	}
	srv.log.Infof("%s format required on instance %d", formatType, srv.Index())

	srv.waitFormat.SetTrue()

	select {
	case <-ctx.Done():
		srv.log.Infof("%s instance %d storage not ready: %s", build.DataPlaneName, srv.Index(), ctx.Err())
	case <-srv.storageReady:
		srv.log.Infof("%s instance %d storage ready", build.DataPlaneName, srv.Index())
	}

	srv.waitFormat.SetFalse()

	return ctx.Err()
}

func (srv *EngineInstance) logScmStorage() error {
	scmMount := path.Dir(srv.superblockPath())

	if scmMount != srv.scmConfig().MountPoint {
		return errors.New("superblock path doesn't match config mountpoint")
	}

	mp, err := srv.scmProvider.GetfsUsage(scmMount)
	if err != nil {
		return err
	}

	srv.log.Infof("SCM @ %s: %s Total/%s Avail", mp.Path,
		humanize.Bytes(mp.TotalBytes), humanize.Bytes(mp.AvailBytes))

	return nil
}
