//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"os"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

func TestIOServerInstance_MountScmDevice(t *testing.T) {
	testDir, cleanup := common.CreateTestDir(t)
	defer cleanup()

	var (
		goodMountPoint = testDir + "/mnt/daos"
		ramCfg         = &ioserver.Config{
			Storage: ioserver.StorageConfig{
				SCM: storage.ScmConfig{
					MountPoint:  goodMountPoint,
					Class:       storage.ScmClassRAM,
					RamdiskSize: 1,
				},
			},
		}
		dcpmCfg = &ioserver.Config{
			Storage: ioserver.StorageConfig{
				SCM: storage.ScmConfig{
					MountPoint: goodMountPoint,
					Class:      storage.ScmClassDCPM,
					DeviceList: []string{"/dev/foo"},
				},
			},
		}
	)

	for name, tc := range map[string]struct {
		ioCfg  *ioserver.Config
		msCfg  *scm.MockSysConfig
		expErr error
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
			ioCfg: ramCfg,
		},
		"mount ramdisk fails": {
			ioCfg: ramCfg,
			msCfg: &scm.MockSysConfig{
				MountErr: errors.New("mount failed"),
			},
			expErr: errors.New("mount failed"),
		},
		"mount dcpm": {
			ioCfg: dcpmCfg,
		},
		"mount dcpm fails": {
			ioCfg: dcpmCfg,
			msCfg: &scm.MockSysConfig{
				MountErr: errors.New("mount failed"),
			},
			expErr: errors.New("mount failed"),
		},
		"mount dcpm fails (missing device)": {
			ioCfg: &ioserver.Config{
				Storage: ioserver.StorageConfig{
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

			if tc.ioCfg == nil {
				tc.ioCfg = &ioserver.Config{}
			}

			runner := ioserver.NewRunner(log, tc.ioCfg)
			mp := scm.NewMockProvider(log, nil, tc.msCfg)
			instance := NewIOServerInstance(log, nil, mp, nil, runner)

			gotErr := instance.MountScmDevice()
			common.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestIOServerInstance_NeedsScmFormat(t *testing.T) {
	const (
		goodMountPoint = "/mnt/daos"
	)
	var (
		ramCfg = &ioserver.Config{
			Storage: ioserver.StorageConfig{
				SCM: storage.ScmConfig{
					MountPoint:  goodMountPoint,
					Class:       storage.ScmClassRAM,
					RamdiskSize: 1,
				},
			},
		}
		dcpmCfg = &ioserver.Config{
			Storage: ioserver.StorageConfig{
				SCM: storage.ScmConfig{
					MountPoint: goodMountPoint,
					Class:      storage.ScmClassDCPM,
					DeviceList: []string{"/dev/foo"},
				},
			},
		}
	)

	for name, tc := range map[string]struct {
		ioCfg          *ioserver.Config
		mbCfg          *scm.MockBackendConfig
		msCfg          *scm.MockSysConfig
		expNeedsFormat bool
		expErr         error
	}{
		"empty config": {
			expErr: errors.New("operation unsupported on SCM class"),
		},
		"check ramdisk fails (IsMounted fails)": {
			ioCfg: ramCfg,
			msCfg: &scm.MockSysConfig{
				IsMountedErr: errors.New("failed to check mount"),
			},
			expErr: errors.New("failed to check mount"),
		},
		"check ramdisk (mounted)": {
			ioCfg: ramCfg,
			msCfg: &scm.MockSysConfig{
				IsMountedBool: true,
			},
			expNeedsFormat: false,
		},
		"check ramdisk (unmounted)": {
			ioCfg:          ramCfg,
			expNeedsFormat: true,
		},
		"check ramdisk (unmounted, mountpoint doesn't exist)": {
			ioCfg: ramCfg,
			msCfg: &scm.MockSysConfig{
				IsMountedErr: os.ErrNotExist,
			},
			expNeedsFormat: true,
		},
		"check dcpm (mounted)": {
			ioCfg: dcpmCfg,
			msCfg: &scm.MockSysConfig{
				IsMountedBool: true,
			},
			expNeedsFormat: false,
		},
		"check dcpm (unmounted, unformatted)": {
			ioCfg: dcpmCfg,
			msCfg: &scm.MockSysConfig{
				GetfsStr: "none",
			},
			expNeedsFormat: true,
		},
		"check dcpm (unmounted, formatted)": {
			ioCfg: dcpmCfg,
			msCfg: &scm.MockSysConfig{
				GetfsStr: "ext4",
			},
			expNeedsFormat: false,
		},
		"check dcpm (unmounted, formatted, mountpoint doesn't exist)": {
			ioCfg: dcpmCfg,
			msCfg: &scm.MockSysConfig{
				IsMountedErr: os.ErrNotExist,
				GetfsStr:     "ext4",
			},
			expNeedsFormat: false,
		},
		"check dcpm fails (IsMounted fails)": {
			ioCfg: dcpmCfg,
			msCfg: &scm.MockSysConfig{
				IsMountedErr: errors.New("failed to check mount"),
			},
			expErr: errors.New("failed to check mount"),
		},
		"check dcpm fails (missing device)": {
			ioCfg: &ioserver.Config{
				Storage: ioserver.StorageConfig{
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

			if tc.ioCfg == nil {
				tc.ioCfg = &ioserver.Config{}
			}

			runner := ioserver.NewRunner(log, tc.ioCfg)
			mp := scm.NewMockProvider(log, tc.mbCfg, tc.msCfg)
			instance := NewIOServerInstance(log, nil, mp, nil, runner)

			gotNeedsFormat, gotErr := instance.NeedsScmFormat()
			common.CmpErr(t, tc.expErr, gotErr)
			if diff := cmp.Diff(tc.expNeedsFormat, gotNeedsFormat); diff != "" {
				t.Fatalf("unexpected needs format (-want, +got):\n%s\n", diff)
			}
		})
	}
}
