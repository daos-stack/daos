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
	"path/filepath"
	"sync"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/provider/system"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

var mockRamCfg = storage.Config{
	ControlMetadata: storage.ControlMetadata{
		Path:       "/dontcare",
		DevicePath: "/dev/dontcare",
	},
	Tiers: storage.TierConfigs{
		{
			Class: storage.ClassRam,
			Scm: storage.ScmConfig{
				MountPoint:  defaultStoragePath,
				RamdiskSize: 1,
			},
		},
		{
			Class: storage.ClassNvme,
			Bdev: storage.BdevConfig{
				DeviceList:  new(storage.BdevDeviceList),
				DeviceRoles: storage.BdevRoles{storage.BdevRoleAll},
			},
		},
	},
}

func TestIOEngineInstance_MountControlMetadata(t *testing.T) {
	for name, tc := range map[string]struct {
		meta   *storage.MockMetadataProvider
		sysCfg *system.MockSysConfig
		expErr error
	}{
		"check mounted fails": {
			sysCfg: &system.MockSysConfig{
				IsMountedErr: errors.New("mock IsMounted"),
			},
			expErr: errors.New("mock IsMounted"),
		},
		"already mounted": {
			sysCfg: &system.MockSysConfig{
				IsMountedBool: true,
			},
			meta: &storage.MockMetadataProvider{
				MountErr: errors.New("mount was called!"),
			},
		},
		"mount fails": {
			meta: &storage.MockMetadataProvider{
				MountErr: errors.New("mock mount"),
			},
			expErr: errors.New("mock mount"),
		},
		"success": {
			meta: &storage.MockMetadataProvider{
				MountRes: &storage.MountResponse{},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.meta == nil {
				tc.meta = &storage.MockMetadataProvider{}
			}

			ec := engine.MockConfig().
				WithStorageControlMetadataPath(mockRamCfg.ControlMetadata.Path).
				WithStorageControlMetadataDevice(mockRamCfg.ControlMetadata.DevicePath)
			runner := engine.NewRunner(log, ec)
			sysProv := system.NewMockSysProvider(log, tc.sysCfg)
			provider := storage.MockProvider(log, 0, &mockRamCfg, sysProv, nil, nil,
				tc.meta)
			instance := NewEngineInstance(log, provider, nil, runner, nil)

			gotErr := instance.MountMetadata()
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestIOEngineInstance_MountScmDevice(t *testing.T) {
	testDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	var (
		goodMountPoint = testDir + "/mnt/daos"
		ramCfg         = &storage.Config{
			Tiers: storage.TierConfigs{
				{
					Class: storage.ClassRam,
					Scm: storage.ScmConfig{
						MountPoint:  goodMountPoint,
						RamdiskSize: 1,
					},
				},
			},
		}
		dcpmCfg = &storage.Config{
			Tiers: storage.TierConfigs{
				{
					Class: storage.ClassDcpm,
					Scm: storage.ScmConfig{
						MountPoint: goodMountPoint,
						DeviceList: []string{"/dev/foo"},
					},
				},
			},
		}
	)

	for name, tc := range map[string]struct {
		cfg    *storage.Config
		msCfg  *system.MockSysConfig
		expErr error
	}{
		"empty config": {
			expErr: storage.ErrNoScmTiers,
		},
		"IsMounted fails": {
			cfg: ramCfg,
			msCfg: &system.MockSysConfig{
				IsMountedErr: errors.New("failed to check mount"),
			},
			expErr: errors.New("failed to check mount"),
		},
		"already mounted": {
			cfg: ramCfg,
			msCfg: &system.MockSysConfig{
				IsMountedBool: true,
			},
		},
		"mount ramdisk": {
			cfg: ramCfg,
		},
		"mount ramdisk fails": {
			cfg: ramCfg,
			msCfg: &system.MockSysConfig{
				MountErr: errors.New("mount failed"),
			},
			expErr: errors.New("mount failed"),
		},
		"mount dcpm": {
			cfg: dcpmCfg,
		},
		"mount dcpm fails": {
			cfg: dcpmCfg,
			msCfg: &system.MockSysConfig{
				MountErr: errors.New("mount failed"),
			},
			expErr: errors.New("mount failed"),
		},
		"mount dcpm fails (missing device)": {
			cfg: &storage.Config{
				Tiers: storage.TierConfigs{
					{
						Class: storage.ClassDcpm,
						Scm: storage.ScmConfig{
							MountPoint: goodMountPoint,
						},
					},
				},
			},
			expErr: storage.ErrInvalidDcpmCount,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.cfg == nil {
				tc.cfg = &storage.Config{}
			}

			ec := engine.MockConfig().WithStorage(tc.cfg.Tiers...)
			runner := engine.NewRunner(log, ec)
			sys := system.NewMockSysProvider(log, tc.msCfg)
			scm := scm.NewMockProvider(log, nil, tc.msCfg)
			provider := storage.MockProvider(log, 0, tc.cfg, sys, scm, nil, nil)
			instance := NewEngineInstance(log, provider, nil, runner, nil)

			gotErr := instance.MountScm()
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestEngineInstance_NeedsScmFormat(t *testing.T) {
	const (
		dev            = "/dev/foo"
		goodMountPoint = "/mnt/daos"
	)
	var (
		ramCfg = engine.MockConfig().WithStorage(
			storage.NewTierConfig().
				WithStorageClass(storage.ClassRam.String()).
				WithScmMountPoint(goodMountPoint).
				WithScmRamdiskSize(1),
		)
		dcpmCfg = engine.MockConfig().WithStorage(
			storage.NewTierConfig().
				WithStorageClass(storage.ClassDcpm.String()).
				WithScmMountPoint(goodMountPoint).
				WithScmDeviceList(dev),
		)
	)

	for name, tc := range map[string]struct {
		engineCfg      *engine.Config
		mbCfg          *scm.MockBackendConfig
		msCfg          *system.MockSysConfig
		expNeedsFormat bool
		expErr         error
	}{
		"empty config": {
			expErr: storage.ErrNoScmTiers,
		},
		"check ramdisk fails (IsMounted fails)": {
			engineCfg: ramCfg,
			msCfg: &system.MockSysConfig{
				IsMountedErr: errors.New("failed to check mount"),
			},
			expErr: errors.New("failed to check mount"),
		},
		"check ramdisk (mounted)": {
			engineCfg: ramCfg,
			msCfg: &system.MockSysConfig{
				IsMountedBool: true,
			},
			expNeedsFormat: false,
		},
		"check ramdisk (unmounted)": {
			engineCfg:      ramCfg,
			expNeedsFormat: true,
		},
		"check ramdisk (unmounted, mountpoint doesn't exist)": {
			engineCfg: ramCfg,
			msCfg: &system.MockSysConfig{
				IsMountedErr: os.ErrNotExist,
			},
			expNeedsFormat: true,
		},
		"check dcpm (mounted)": {
			engineCfg: dcpmCfg,
			msCfg: &system.MockSysConfig{
				IsMountedBool: true,
			},
			expNeedsFormat: false,
		},
		"check dcpm (unmounted, unformatted)": {
			engineCfg: dcpmCfg,
			msCfg: &system.MockSysConfig{
				GetfsStr: "none",
			},
			expNeedsFormat: true,
		},
		"check dcpm (unmounted, formatted)": {
			engineCfg: dcpmCfg,
			msCfg: &system.MockSysConfig{
				GetfsStr: "ext4",
			},
			expNeedsFormat: false,
		},
		"check dcpm (unmounted, formatted, mountpoint doesn't exist)": {
			engineCfg: dcpmCfg,
			msCfg: &system.MockSysConfig{
				IsMountedErr: os.ErrNotExist,
				GetfsStr:     "ext4",
			},
			expErr:         storage.FaultDeviceWithFsNoMountpoint(dev, goodMountPoint),
			expNeedsFormat: false,
		},
		"check dcpm fails (IsMounted fails)": {
			engineCfg: dcpmCfg,
			msCfg: &system.MockSysConfig{
				IsMountedErr: errors.New("failed to check mount"),
			},
			expErr: errors.New("failed to check mount"),
		},
		"check dcpm fails (missing device)": {
			engineCfg: engine.MockConfig().WithStorage(
				storage.NewTierConfig().
					WithStorageClass(storage.ClassDcpm.String()).
					WithScmMountPoint(goodMountPoint)),
			expErr: storage.ErrInvalidDcpmCount,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.engineCfg == nil {
				tc.engineCfg = &engine.Config{}
			}

			runner := engine.NewRunner(log, tc.engineCfg)
			mp := storage.NewProvider(log, 0, &tc.engineCfg.Storage,
				system.NewMockSysProvider(log, tc.msCfg),
				scm.NewMockProvider(log, tc.mbCfg, tc.msCfg),
				nil, nil)
			instance := NewEngineInstance(log, mp, nil, runner, nil)

			gotNeedsFormat, gotErr := instance.GetStorage().ScmNeedsFormat()
			test.CmpErr(t, tc.expErr, gotErr)
			if diff := cmp.Diff(tc.expNeedsFormat, gotNeedsFormat); diff != "" {
				t.Fatalf("unexpected needs format (-want, +got):\n%s\n", diff)
			}
		})
	}
}

type tally struct {
	sync.Mutex
	evtDesc      string
	storageReady chan bool
	finished     chan struct{}
}

func newTally(sr chan bool) *tally {
	return &tally{
		storageReady: sr,
		finished:     make(chan struct{}),
	}
}

func (tly *tally) fakePublish(evt *events.RASEvent) {
	tly.Lock()
	defer tly.Unlock()

	tly.evtDesc = evt.Msg
	close(tly.storageReady)
	close(tly.finished)
}

func TestIOEngineInstance_awaitStorageReady(t *testing.T) {
	errStarted := errors.New("already started")
	dev := "/dev/foo"
	mnt := "/mnt/test"
	dcpmCfg := engine.MockConfig().WithStorage(
		storage.NewTierConfig().
			WithStorageClass(storage.ClassDcpm.String()).
			WithScmMountPoint(mnt).
			WithScmDeviceList(dev),
	)

	for name, tc := range map[string]struct {
		engineStarted   bool
		storageCfg      *storage.Config
		fsStr           string
		fsErr           error
		sbSet           bool
		engineIndex     uint32
		isMounted       bool
		isMountedErr    error
		mountErr        error
		unmountErr      error
		readErr         error
		metaNeedsFmt    bool
		metaNeedsFmtErr error
		expNoWait       bool
		expFmtType      string
		expErr          error
	}{
		"already started": {
			engineStarted: true,
			fsStr:         "ext4",
			expErr:        errStarted,
		},
		"no need to format and superblock set": {
			sbSet:     true,
			fsStr:     "ext4",
			isMounted: true,
			expNoWait: true,
		},
		"no need to format and superblock set; mount fails": {
			fsStr:      "ext4",
			mountErr:   errors.New("bad mount"),
			expFmtType: "Metadata",
		},
		"filesystem exists and superblock set; mountpoint missing": {
			sbSet:        true,
			fsStr:        "ext4",
			isMountedErr: os.ErrNotExist,
			expErr:       storage.FaultDeviceWithFsNoMountpoint(dev, mnt),
		},
		"mount check fails": {
			sbSet:        true,
			fsStr:        "ext4",
			isMountedErr: errors.New("fail"),
			expFmtType:   "SCM",
		},
		"no need to format: superblock exists but not yet set": {
			fsStr:     "ext4",
			isMounted: true,
			expNoWait: true,
		},
		"superblock not found; needs metadata format": {
			fsStr:      "ext4",
			readErr:    os.ErrNotExist,
			expFmtType: "Metadata",
		},
		"needs scm format": {
			fsStr:      "none",
			expFmtType: "SCM",
		},
		"engine index 1; needs metadata format": {
			engineIndex: 1,
			fsStr:       "ext4",
			readErr:     os.ErrNotExist,
			expFmtType:  "Metadata",
		},
		"metadata path with dcpm": {
			storageCfg: &storage.Config{
				ControlMetadata: storage.ControlMetadata{
					Path:       "/dontcare",
					DevicePath: "/dev/dontcare",
				},
				Tiers: storage.TierConfigs{
					{Class: storage.ClassDcpm},
				},
			},
			expErr: storage.FaultBdevConfigRolesWithDCPM,
		},
		"no device roles with metadata path configured": {
			storageCfg: &storage.Config{
				ControlMetadata: storage.ControlMetadata{
					Path:       "/dontcare",
					DevicePath: "/dev/dontcare",
				},
				Tiers: storage.TierConfigs{
					{Class: storage.ClassRam},
				},
			},
			expErr: storage.FaultBdevConfigControlMetadataNoRoles,
		},
		"metadata path; no metadata format needed; scm format fails": {
			storageCfg: &mockRamCfg,
			isMounted:  true,
			unmountErr: errors.New("ramdisk busy"),
			expErr:     errors.New("format ramdisk: failed to clear existing mount: failed to unmount"),
		},
		"metadata path; no metadata format needed; scm format succeeds; superblock intact": {
			storageCfg: &mockRamCfg,
			isMounted:  true,
			expNoWait:  true,
		},
		"metadata path; no metadata format needed; scm format succeeds; no superblock": {
			storageCfg: &mockRamCfg,
			isMounted:  true,
			readErr:    os.ErrNotExist,
			expFmtType: "Metadata",
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			trc := &engine.TestRunnerConfig{}
			if tc.engineStarted {
				trc.Running.SetTrue()
			}
			runner := engine.NewTestRunner(trc, dcpmCfg)

			if tc.storageCfg == nil {
				tc.storageCfg = &dcpmCfg.Storage
			}

			msc := system.MockSysConfig{
				IsMountedBool: tc.isMounted,
				IsMountedErr:  tc.isMountedErr,
				MountErr:      tc.mountErr,
				UnmountErr:    tc.unmountErr,
				GetfsStr:      tc.fsStr,
				GetfsErr:      tc.fsErr,
			}
			if tc.readErr != nil {
				storagePath := mnt
				ctlMD := tc.storageCfg.ControlMetadata
				if ctlMD.HasPath() {
					storagePath = ctlMD.EngineDirectory(uint(tc.engineIndex))
				}
				sbPath := filepath.Join(storagePath, "superblock")
				t.Logf("setting readfile err %s for path %s", tc.readErr.Error(), sbPath)
				msc.ReadFileErrors = map[string]error{sbPath: tc.readErr}
			}
			smbc := scm.MockBackendConfig{}
			mmp := &storage.MockMetadataProvider{
				NeedsFormatRes: tc.metaNeedsFmt,
				NeedsFormatErr: tc.metaNeedsFmtErr,
			}

			mp := storage.NewProvider(log, 0, tc.storageCfg,
				system.NewMockSysProvider(log, &msc),
				scm.NewMockProvider(log, &smbc, &msc),
				nil, mmp)
			engine := NewEngineInstance(log, mp, nil, runner, nil)

			engine.setIndex(tc.engineIndex)

			if tc.sbSet {
				engine.setSuperblock(&Superblock{
					Rank: ranklist.NewRankPtr(0), ValidRank: true,
				})
			}

			tly1 := newTally(engine.storageReady)

			hn, _ := os.Hostname()
			engine.OnAwaitFormat(createPublishFormatRequiredFunc(tly1.fakePublish, hn))

			ctx, cancel := context.WithTimeout(test.Context(t), time.Millisecond*100)
			defer cancel()

			gotErr := engine.awaitStorageReady(ctx)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil || tc.expNoWait == true {
				return
			}

			select {
			case <-tly1.finished:
			case <-ctx.Done():
				t.Fatal("unexpected timeout waiting for format required event")
			}

			expDescription := fmt.Sprintf("DAOS engine %d requires a %s format",
				tc.engineIndex, tc.expFmtType)
			if diff := cmp.Diff(expDescription, tly1.evtDesc); diff != "" {
				t.Fatalf("unexpected event description (-want, +got):\n%s\n", diff)
			}
		})
	}
}
