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
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
	"github.com/daos-stack/daos/src/control/system"
)

// scmConfig returns the scm configuration assigned to this instance.
func (ei *EngineInstance) scmConfig() storage.ScmConfig {
	return ei.runner.GetConfig().Storage.SCM
}

// bdevConfig returns the block device configuration assigned to this instance.
func (ei *EngineInstance) bdevConfig() storage.BdevConfig {
	return ei.runner.GetConfig().Storage.Bdev
}

// MountScmDevice mounts the configured SCM device (DCPM or ramdisk emulation)
// at the mountpoint specified in the configuration. If the device is already
// mounted, the function returns nil, indicating success.
func (ei *EngineInstance) MountScmDevice() error {
	scmCfg := ei.scmConfig()

	isMount, err := ei.scmProvider.IsMounted(scmCfg.MountPoint)
	if err != nil && !os.IsNotExist(errors.Cause(err)) {
		return errors.WithMessage(err, "failed to check SCM mount")
	}
	if isMount {
		return nil
	}

	ei.log.Debugf("attempting to mount existing SCM dir %s\n", scmCfg.MountPoint)

	var res *scm.MountResponse
	switch scmCfg.Class {
	case storage.ScmClassRAM:
		res, err = ei.scmProvider.MountRamdisk(scmCfg.MountPoint, uint(scmCfg.RamdiskSize))
	case storage.ScmClassDCPM:
		if len(scmCfg.DeviceList) != 1 {
			err = scm.FaultFormatInvalidDeviceCount
			break
		}
		res, err = ei.scmProvider.MountDcpm(scmCfg.DeviceList[0], scmCfg.MountPoint)
	default:
		err = errors.New(scm.MsgClassNotSupported)
	}
	if err != nil {
		return errors.WithMessage(err, "mounting existing scm dir")
	}
	ei.log.Debugf("%s mounted: %t", res.Target, res.Mounted)

	return nil
}

// NeedsScmFormat probes the configured instance storage and determines whether
// or not it requires a format operation before it can be used.
func (ei *EngineInstance) NeedsScmFormat() (bool, error) {
	scmCfg := ei.scmConfig()

	ei.log.Debugf("%s: checking formatting", scmCfg.MountPoint)

	ei.RLock()
	defer ei.RUnlock()

	req, err := scm.CreateFormatRequest(scmCfg, false)
	if err != nil {
		return false, err
	}

	res, err := ei.scmProvider.CheckFormat(*req)
	if err != nil {
		return false, err
	}

	needsFormat := !res.Mounted && !res.Mountable
	ei.log.Debugf("%s (%s) needs format: %t", scmCfg.MountPoint, scmCfg.Class, needsFormat)
	return needsFormat, nil
}

// NotifyStorageReady releases any blocks on awaitStorageReady().
func (ei *EngineInstance) NotifyStorageReady() {
	go func() {
		ei.storageReady <- true
	}()
}

// publishFormatRequiredFn returns onAwaitFormatFn which will publish an
// event using the provided publish function to indicate that host is awaiting
// storage format.
func publishFormatRequiredFn(publishFn func(*events.RASEvent), hostname string) onAwaitFormatFn {
	return func(_ context.Context, engineIdx uint32, formatType string) error {
		evt := events.NewEngineFormatRequiredEvent(hostname, engineIdx, formatType).
			WithRank(uint32(system.NilRank))
		publishFn(evt)

		return nil
	}
}

// awaitStorageReady blocks until instance has storage available and ready to be used.
func (ei *EngineInstance) awaitStorageReady(ctx context.Context, skipMissingSuperblock bool) error {
	idx := ei.Index()

	if ei.isStarted() {
		return errors.Errorf("can't wait for storage: instance %d already started", idx)
	}

	ei.log.Infof("Checking %s instance %d storage ...", build.DataPlaneName, idx)

	needsScmFormat, err := ei.NeedsScmFormat()
	if err != nil {
		ei.log.Errorf("instance %d: failed to check storage formatting: %s", idx, err)
		needsScmFormat = true
	}

	if !needsScmFormat {
		if skipMissingSuperblock {
			return nil
		}
		ei.log.Debugf("instance %d: no SCM format required; checking for superblock", idx)
		needsSuperblock, err := ei.NeedsSuperblock()
		if err != nil {
			ei.log.Errorf("instance %d: failed to check instance superblock: %s", idx, err)
		}
		if !needsSuperblock {
			ei.log.Debugf("instance %d: superblock not needed", idx)
			return nil
		}
	}

	if skipMissingSuperblock {
		return FaultScmUnmanaged(ei.scmConfig().MountPoint)
	}

	// by this point we need superblock and possibly scm format
	formatType := "SCM"
	if !needsScmFormat {
		formatType = "Metadata"
	}
	ei.log.Infof("%s format required on instance %d", formatType, idx)

	ei.waitFormat.SetTrue()
	// After we know that the instance is awaiting format, fire off
	// any callbacks that are waiting for this state.
	for _, fn := range ei.onAwaitFormat {
		if err := fn(ctx, idx, formatType); err != nil {
			return err
		}
	}

	select {
	case <-ctx.Done():
		ei.log.Infof("%s instance %d storage not ready: %s", build.DataPlaneName, idx, ctx.Err())
	case <-ei.storageReady:
		ei.log.Infof("%s instance %d storage ready", build.DataPlaneName, idx)
	}

	ei.waitFormat.SetFalse()

	return ctx.Err()
}

func (ei *EngineInstance) logScmStorage() error {
	scmMount := path.Dir(ei.superblockPath())

	if scmMount != ei.scmConfig().MountPoint {
		return errors.New("superblock path doesn't match config mountpoint")
	}

	mp, err := ei.scmProvider.GetfsUsage(scmMount)
	if err != nil {
		return err
	}

	ei.log.Infof("SCM @ %s: %s Total/%s Avail", mp.Path,
		humanize.Bytes(mp.TotalBytes), humanize.Bytes(mp.AvailBytes))

	return nil
}
