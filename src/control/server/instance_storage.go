//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"os"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// GetStorage retrieve the storage provider for an engine instance.
func (ei *EngineInstance) GetStorage() *storage.Provider {
	return ei.storage
}

// MountMetadata mounts the configured control metadata location.
func (ei *EngineInstance) MountMetadata() error {
	ei.log.Debug("checking if metadata is mounted")
	isMounted, err := ei.storage.ControlMetadataIsMounted()
	if err != nil {
		return errors.Wrap(err, "checking if metadata is mounted")
	}

	ei.log.Debugf("IsMounted: %v", isMounted)
	if isMounted {
		return nil
	}

	return ei.storage.MountControlMetadata()
}

// MountScm mounts the configured SCM device (DCPM or ramdisk emulation)
// at the mountpoint specified in the configuration. If the device is already
// mounted, the function returns nil, indicating success.
func (ei *EngineInstance) MountScm() error {
	isMounted, err := ei.storage.ScmIsMounted()
	if err != nil && !os.IsNotExist(errors.Cause(err)) {
		return errors.WithMessage(err, "failed to check SCM mount")
	}
	if isMounted {
		return nil
	}

	return ei.storage.MountScm()
}

// NotifyStorageReady releases any blocks on awaitStorageReady().
func (ei *EngineInstance) NotifyStorageReady() {
	go func() {
		ei.storageReady <- true
	}()
}

// createPublishFormatRequiredFunc returns onAwaitFormatFn which will publish an
// event using the provided publish function to indicate that host is awaiting
// storage format.
func createPublishFormatRequiredFunc(publish func(*events.RASEvent), hostname string) onAwaitFormatFn {
	return func(_ context.Context, engineIdx uint32, formatType string) error {
		evt := events.NewEngineFormatRequiredEvent(hostname, engineIdx, formatType).
			WithRank(uint32(ranklist.NilRank))
		publish(evt)

		return nil
	}
}

// awaitStorageReady blocks until instance has storage available and ready to be used.
func (ei *EngineInstance) awaitStorageReady(ctx context.Context, skipMissingSuperblock bool) error {
	idx := ei.Index()

	if ei.IsStarted() {
		return errors.Errorf("can't wait for storage: instance %d already started", idx)
	}

	ei.log.Infof("Checking %s instance %d storage ...", build.DataPlaneName, idx)

	needsMetaFormat, err := ei.storage.ControlMetadataNeedsFormat()
	if err != nil {
		ei.log.Errorf("failed to check control metadata storage formatting: %s", err)
		needsMetaFormat = true
	}
	ei.log.Debugf("needsMetaFormat: %t", needsMetaFormat)

	needsScmFormat, err := ei.storage.ScmNeedsFormat()
	if err != nil {
		ei.log.Errorf("instance %d: failed to check storage formatting: %s", idx, err)
		needsScmFormat = true
	}
	ei.log.Debugf("needsScmFormat: %t", needsScmFormat)

	if !needsMetaFormat && ei.storage.ControlMetadataPathConfigured() {
		cfg, err := ei.storage.GetScmConfig()
		if err != nil {
			return err
		}
		if (cfg.Class == "ram") && ei.storage.BdevRoleMetaConfigured() {
			ei.log.Debugf("scm class is ram and bdev role meta configured")
			err := ei.storage.FormatScm(true)
			if err != nil {
				ei.log.Errorf("instance %d: failed to setup the scm: %s", idx, err)
			} else {
				ei.log.Debugf("remounted scm")
				needsScmFormat = false
			}
		}
	}

	if !needsMetaFormat && !needsScmFormat {
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
		ei.log.Debugf("instance %d: superblock needed", idx)
	}

	if needsScmFormat {
		cfg, err := ei.storage.GetScmConfig()
		if err != nil {
			return err
		}
		if skipMissingSuperblock {
			return FaultScmUnmanaged(cfg.Scm.MountPoint)
		}
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
	mp, err := ei.storage.GetScmUsage()
	if err != nil {
		return err
	}

	ei.log.Infof("SCM @ %s: %s Total/%s Avail", mp.Path,
		humanize.IBytes(mp.TotalBytes), humanize.IBytes(mp.AvailBytes))

	return nil
}

// ScanBdevTiers calls in to the private engine storage provider to scan bdev
// tiers. Scan will avoid using any cached results if direct is set to true.
func (ei *EngineInstance) ScanBdevTiers() ([]storage.BdevTierScanResult, error) {
	isUp := ei.IsReady()
	upDn := "down"
	if isUp {
		upDn = "up"
	}
	ei.log.Debugf("scanning engine-%d bdev tiers while engine is %s", ei.Index(), upDn)

	return ei.storage.ScanBdevTiers(!isUp)
}
