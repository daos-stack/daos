//
// (C) Copyright 2020-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"
	"os"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// GetStorage retrieve the storage provider for an engine instance.
func (ei *EngineInstance) GetStorage() *storage.Provider {
	return ei.storage
}

// MountMetadata mounts the configured control metadata location.
func (ei *EngineInstance) MountMetadata() error {
	msgIdx := fmt.Sprintf("instance %d", ei.Index())

	msgCheck := fmt.Sprintf("%s: checking if metadata is mounted", msgIdx)
	ei.log.Trace(msgCheck)

	isMounted, err := ei.storage.ControlMetadataIsMounted()
	if err != nil {
		return errors.WithMessage(err, msgCheck)
	}

	ei.log.Debugf("%s: IsMounted: %v", msgIdx, isMounted)
	if isMounted {
		return nil
	}

	return ei.storage.MountControlMetadata()
}

// MountScm mounts the configured SCM device (DCPM or ramdisk emulation)
// at the mountpoint specified in the configuration. If the device is already
// mounted, the function returns nil, indicating success.
func (ei *EngineInstance) MountScm() error {
	msgIdx := fmt.Sprintf("instance %d", ei.Index())

	msgCheck := fmt.Sprintf("%s: checking if scm is mounted", msgIdx)
	ei.log.Trace(msgCheck)

	isMounted, err := ei.storage.ScmIsMounted()
	if err != nil && !os.IsNotExist(errors.Cause(err)) {
		return errors.WithMessage(err, msgCheck)
	}

	ei.log.Debugf("%s: IsMounted: %v", msgIdx, isMounted)
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

func (ei *EngineInstance) checkScmNeedFormat() (bool, error) {
	msgIdx := fmt.Sprintf("instance %d", ei.Index())

	if ei.storage.ControlMetadataPathConfigured() {
		cfg, err := ei.storage.GetScmConfig()
		if err != nil {
			return false, err
		}
		if cfg.Class != "ram" {
			return false, storage.FaultBdevConfigRolesWithDCPM
		}
		if !ei.storage.BdevRoleMetaConfigured() && !ei.storage.GetControlMetadata().DisableMdOnSSD {
			return false, storage.FaultBdevConfigControlMetadataNoRoles
		}
		ei.log.Debugf("scm class is ram and bdev role meta configured")

		return true, nil
	}

	needsScmFormat, err := ei.storage.ScmNeedsFormat()
	if err != nil {
		if fault.IsFaultCode(err, code.StorageDeviceWithFsNoMountpoint) {
			return false, err
		}
		ei.log.Errorf("%s: failed to check storage formatting: %s", msgIdx, err)

		return true, nil
	}

	return needsScmFormat, nil
}

// awaitStorageReady blocks until instance has storage available and ready to be used.
func (ei *EngineInstance) awaitStorageReady(ctx context.Context) error {
	idx := ei.Index()
	msgIdx := fmt.Sprintf("instance %d", idx)

	if ei.IsStarted() {
		return errors.Errorf("can't wait for storage: %s already started", msgIdx)
	}

	ei.log.Infof("Checking %s %s storage ...", build.DataPlaneName, msgIdx)

	needsMetaFormat, err := ei.storage.ControlMetadataNeedsFormat()
	if err != nil {
		ei.log.Errorf("%s: failed to check control metadata storage formatting: %s",
			msgIdx, err)
		needsMetaFormat = true
	}
	ei.log.Debugf("%s: needsMetaFormat: %t", msgIdx, needsMetaFormat)

	needsScmFormat, err := ei.checkScmNeedFormat()
	if err != nil {
		return err
	}
	ei.log.Debugf("%s: needsScmFormat: %t", msgIdx, needsScmFormat)

	// Always reformat ramdisk in MD-on-SSD mode if control metadata intact.
	if ei.storage.ControlMetadataPathConfigured() && !needsMetaFormat {
		if err := ei.storage.FormatScm(true); err != nil {
			return errors.Wrapf(err, "%s: format ramdisk", msgIdx)
		}
		needsScmFormat = false
	}

	if !needsMetaFormat && !needsScmFormat {
		ei.log.Debugf("%s: no SCM format required; checking for superblock", msgIdx)
		needsSuperblock, err := ei.needsSuperblock()
		if err != nil {
			ei.log.Errorf("%s: failed to check instance superblock: %s", msgIdx, err)
		}
		if !needsSuperblock {
			ei.log.Debugf("%s: superblock not needed", msgIdx)
			return nil
		}
		ei.log.Debugf("%s: superblock needed", msgIdx)
	}

	// by this point we need superblock and possibly scm format
	formatType := "SCM"
	if !needsScmFormat {
		formatType = "Metadata"
	}
	ei.log.Infof("%s format required on %s", formatType, msgIdx)

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
		ei.log.Infof("%s %s storage not ready: %s", build.DataPlaneName, msgIdx, ctx.Err())
	case <-ei.storageReady:
		ei.log.Infof("%s %s storage ready", build.DataPlaneName, msgIdx)
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
