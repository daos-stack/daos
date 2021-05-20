//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"
	"os"
	"sync"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
	"github.com/daos-stack/daos/src/control/system"
)

func TestIOEngineInstance_MountScmDevice(t *testing.T) {
	testDir, cleanup := common.CreateTestDir(t)
	defer cleanup()

	var (
		goodMountPoint = testDir + "/mnt/daos"
		ramCfg         = &engine.Config{
			Storage: engine.StorageConfig{
				SCM: storage.ScmConfig{
					MountPoint:  goodMountPoint,
					Class:       storage.ScmClassRAM,
					RamdiskSize: 1,
				},
			},
		}
		dcpmCfg = &engine.Config{
			Storage: engine.StorageConfig{
				SCM: storage.ScmConfig{
					MountPoint: goodMountPoint,
					Class:      storage.ScmClassDCPM,
					DeviceList: []string{"/dev/foo"},
				},
			},
		}
	)

	for name, tc := range map[string]struct {
		engineCfg *engine.Config
		msCfg     *scm.MockSysConfig
		expErr    error
	}{
		"empty config": {
			expErr: errors.New("operation unsupported on SCM class"),
		},
		"IsMounted fails": {
			msCfg: &scm.MockSysConfig{
				IsMountedErr: errors.New("failed to check mount"),
			},
			expErr: errors.New("failed to check mount"),
		},
		"already mounted": {
			msCfg: &scm.MockSysConfig{
				IsMountedBool: true,
			},
		},
		"mount ramdisk": {
			engineCfg: ramCfg,
		},
		"mount ramdisk fails": {
			engineCfg: ramCfg,
			msCfg: &scm.MockSysConfig{
				MountErr: errors.New("mount failed"),
			},
			expErr: errors.New("mount failed"),
		},
		"mount dcpm": {
			engineCfg: dcpmCfg,
		},
		"mount dcpm fails": {
			engineCfg: dcpmCfg,
			msCfg: &scm.MockSysConfig{
				MountErr: errors.New("mount failed"),
			},
			expErr: errors.New("mount failed"),
		},
		"mount dcpm fails (missing device)": {
			engineCfg: &engine.Config{
				Storage: engine.StorageConfig{
					SCM: storage.ScmConfig{
						MountPoint: goodMountPoint,
						Class:      storage.ScmClassDCPM,
					},
				},
			},
			expErr: scm.FaultFormatInvalidDeviceCount,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.engineCfg == nil {
				tc.engineCfg = &engine.Config{}
			}

			runner := engine.NewRunner(log, tc.engineCfg)
			mp := scm.NewMockProvider(log, nil, tc.msCfg)
			instance := NewEngineInstance(log, nil, mp, nil, runner)

			gotErr := instance.MountScmDevice()
			common.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestEngineInstance_NeedsScmFormat(t *testing.T) {
	const (
		goodMountPoint = "/mnt/daos"
	)
	var (
		ramCfg = &engine.Config{
			Storage: engine.StorageConfig{
				SCM: storage.ScmConfig{
					MountPoint:  goodMountPoint,
					Class:       storage.ScmClassRAM,
					RamdiskSize: 1,
				},
			},
		}
		dcpmCfg = &engine.Config{
			Storage: engine.StorageConfig{
				SCM: storage.ScmConfig{
					MountPoint: goodMountPoint,
					Class:      storage.ScmClassDCPM,
					DeviceList: []string{"/dev/foo"},
				},
			},
		}
	)

	for name, tc := range map[string]struct {
		engineCfg      *engine.Config
		mbCfg          *scm.MockBackendConfig
		msCfg          *scm.MockSysConfig
		expNeedsFormat bool
		expErr         error
	}{
		"empty config": {
			expErr: errors.New("operation unsupported on SCM class"),
		},
		"check ramdisk fails (IsMounted fails)": {
			engineCfg: ramCfg,
			msCfg: &scm.MockSysConfig{
				IsMountedErr: errors.New("failed to check mount"),
			},
			expErr: errors.New("failed to check mount"),
		},
		"check ramdisk (mounted)": {
			engineCfg: ramCfg,
			msCfg: &scm.MockSysConfig{
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
			msCfg: &scm.MockSysConfig{
				IsMountedErr: os.ErrNotExist,
			},
			expNeedsFormat: true,
		},
		"check dcpm (mounted)": {
			engineCfg: dcpmCfg,
			msCfg: &scm.MockSysConfig{
				IsMountedBool: true,
			},
			expNeedsFormat: false,
		},
		"check dcpm (unmounted, unformatted)": {
			engineCfg: dcpmCfg,
			msCfg: &scm.MockSysConfig{
				GetfsStr: "none",
			},
			expNeedsFormat: true,
		},
		"check dcpm (unmounted, formatted)": {
			engineCfg: dcpmCfg,
			msCfg: &scm.MockSysConfig{
				GetfsStr: "ext4",
			},
			expNeedsFormat: false,
		},
		"check dcpm (unmounted, formatted, mountpoint doesn't exist)": {
			engineCfg: dcpmCfg,
			msCfg: &scm.MockSysConfig{
				IsMountedErr: os.ErrNotExist,
				GetfsStr:     "ext4",
			},
			expNeedsFormat: false,
		},
		"check dcpm fails (IsMounted fails)": {
			engineCfg: dcpmCfg,
			msCfg: &scm.MockSysConfig{
				IsMountedErr: errors.New("failed to check mount"),
			},
			expErr: errors.New("failed to check mount"),
		},
		"check dcpm fails (missing device)": {
			engineCfg: &engine.Config{
				Storage: engine.StorageConfig{
					SCM: storage.ScmConfig{
						MountPoint: goodMountPoint,
						Class:      storage.ScmClassDCPM,
					},
				},
			},
			expErr: scm.FaultFormatInvalidDeviceCount,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.engineCfg == nil {
				tc.engineCfg = &engine.Config{}
			}

			runner := engine.NewRunner(log, tc.engineCfg)
			mp := scm.NewMockProvider(log, tc.mbCfg, tc.msCfg)
			instance := NewEngineInstance(log, nil, mp, nil, runner)

			gotNeedsFormat, gotErr := instance.NeedsScmFormat()
			common.CmpErr(t, tc.expErr, gotErr)
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
	dcpmCfg := &engine.Config{
		Storage: engine.StorageConfig{
			SCM: storage.ScmConfig{
				MountPoint: "/mnt/test",
				Class:      storage.ScmClassDCPM,
				DeviceList: []string{"/dev/foo"},
			},
		},
	}

	for name, tc := range map[string]struct {
		engineStarted  bool
		needsScmFormat bool
		hasSB          bool
		skipMissingSB  bool
		engineIndex    uint32
		expFmtType     string
		expErr         error
	}{
		"already started": {
			engineStarted: true,
			expErr:        errStarted,
		},
		"needs format but skip missing superblock": {
			needsScmFormat: true,
			skipMissingSB:  true,
			expErr:         FaultScmUnmanaged("/mnt/test"),
		},
		"no need to format and skip missing superblock": {
			skipMissingSB: true,
		},
		"no need to format and existing superblock": {
			hasSB: true,
		},
		"needs scm format": {
			needsScmFormat: true,
			expFmtType:     "SCM",
		},
		"needs metadata format": {
			expFmtType: "Metadata",
		},
		"engine index 1": {
			engineIndex: 1,
			expFmtType:  "Metadata",
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			trc := &engine.TestRunnerConfig{}
			if tc.engineStarted {
				trc.Running.SetTrue()
			}
			runner := engine.NewTestRunner(trc, dcpmCfg)

			fs := "none"
			if !tc.needsScmFormat {
				fs = "ext4"
			}
			mp := scm.NewMockProvider(log, nil, &scm.MockSysConfig{GetfsStr: fs})

			engine := NewEngineInstance(log, nil, mp, nil, runner)
			engine.setIndex(tc.engineIndex)

			if tc.hasSB {
				engine.setSuperblock(&Superblock{
					Rank: system.NewRankPtr(0), ValidRank: true,
				})
			}

			tly1 := newTally(engine.storageReady)

			engine.OnAwaitFormat(publishFormatRequiredFn(tly1.fakePublish, hostname()))

			ctx, cancel := context.WithTimeout(context.Background(), time.Millisecond*100)
			defer cancel()

			gotErr := engine.awaitStorageReady(ctx, tc.skipMissingSB)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr == errStarted || tc.skipMissingSB == true || tc.hasSB == true {
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
