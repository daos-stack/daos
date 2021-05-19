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
	"github.com/daos-stack/daos/src/control/system"
)

// MountScm mounts the configured SCM (DCPM or ramdisk emulation)
// at the mountpoint specified in the configuration. If the SCM is already
// mounted, the function returns nil, indicating success.
func (ei *EngineInstance) MountScm() error {
	isMount, err := ei.storage.ScmIsMounted()
	if err != nil && !os.IsNotExist(errors.Cause(err)) {
		return errors.WithMessage(err, "failed to check SCM mount")
	}
	if isMount {
		return nil
	}

	return ei.storage.MountScm()
}

// NeedsScmFormat probes the configured instance storage and determines whether
// or not it requires a format operation before it can be used.
func (ei *EngineInstance) NeedsScmFormat() (bool, error) {
	return ei.storage.ScmNeedsFormat()
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

	cfg, err := ei.storage.GetScmConfig()
	if err != nil {
		return err
	}
	if skipMissingSuperblock {
		return FaultScmUnmanaged(cfg.Scm.MountPoint)
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

	cfg, err := ei.storage.GetScmConfig()
	if err != nil {
		return err
	}
	if scmMount != cfg.Scm.MountPoint {
		return errors.New("superblock path doesn't match config mountpoint")
	}

	mp, err := ei.storage.GetScmUsage()
	if err != nil {
		return err
	}

	ei.log.Infof("SCM @ %s: %s Total/%s Avail", mp.Path,
		humanize.Bytes(mp.TotalBytes), humanize.Bytes(mp.AvailBytes))

	return nil
}
