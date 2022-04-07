//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package scm

import (
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

var (
	mm               = MockModule(nil)
	defaultModule    = &mm
	defaultNamespace = &storage.ScmNamespace{}
)

func TestProvider_Scan(t *testing.T) {
	for name, tc := range map[string]struct {
		rescan          bool
		discoverErr     error
		discoverRes     storage.ScmModules
		getNamespaceErr error
		getNamespaceRes storage.ScmNamespaces
		getStateErr     error
		expResponse     *storage.ScmScanResponse
	}{
		"no modules": {
			discoverRes: storage.ScmModules{},
			expResponse: &storage.ScmScanResponse{
				Modules: storage.ScmModules{},
			},
		},
		"no namespaces": {
			discoverRes:     storage.ScmModules{defaultModule},
			getNamespaceRes: storage.ScmNamespaces{},
			expResponse: &storage.ScmScanResponse{
				Modules:    storage.ScmModules{defaultModule},
				Namespaces: storage.ScmNamespaces{},
			},
		},
		"ok": {
			expResponse: &storage.ScmScanResponse{
				Modules:    storage.ScmModules{defaultModule},
				Namespaces: storage.ScmNamespaces{defaultNamespace},
			},
		},
		"rescan": {
			rescan: true,
			expResponse: &storage.ScmScanResponse{
				Modules:    storage.ScmModules{defaultModule},
				Namespaces: storage.ScmNamespaces{defaultNamespace},
			},
		},
		"ndctl missing": {
			getNamespaceErr: FaultMissingNdctl,
			expResponse: &storage.ScmScanResponse{
				Modules:    storage.ScmModules{defaultModule},
				Namespaces: nil,
			},
		},
		"Discover fails": {
			discoverErr: FaultDiscoveryFailed,
		},
		"GetPmemState fails": {
			getStateErr: errors.New("getstate failed"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.discoverRes == nil {
				tc.discoverRes = storage.ScmModules{defaultModule}
			}
			if tc.getNamespaceRes == nil {
				tc.getNamespaceRes = storage.ScmNamespaces{defaultNamespace}
			}
			mbc := &MockBackendConfig{
				DiscoverRes:         tc.discoverRes,
				DiscoverErr:         tc.discoverErr,
				GetPmemNamespaceRes: tc.getNamespaceRes,
				GetPmemNamespaceErr: tc.getNamespaceErr,
				GetPmemStateErr:     tc.getStateErr,
			}
			p := NewMockProvider(log, mbc, nil)
			cmpRes := func(t *testing.T, want, got *storage.ScmScanResponse) {
				t.Helper()
				if diff := cmp.Diff(want, got); diff != "" {
					t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
				}
			}

			res, err := p.Scan(storage.ScmScanRequest{})
			if err != nil {
				switch err {
				case FaultMissingNdctl:
					cmpRes(t, tc.expResponse, res)
					return
				case tc.discoverErr, tc.getStateErr:
					return
				default:
					t.Fatal(err)
				}
			}
			cmpRes(t, tc.expResponse, res)

			// TODO: Try to simulate finding something new?
			// For now, just make sure nothing breaks.
			res, err = p.Scan(storage.ScmScanRequest{Rescan: tc.rescan})
			if err != nil {
				t.Fatal(err)
			}
			cmpRes(t, tc.expResponse, res)
		})
	}
}

func TestProvider_Prepare(t *testing.T) {
	for name, tc := range map[string]struct {
		startInitialized bool
		reset            bool
		shouldReboot     bool
		discoverErr      error
		getNamespaceErr  error
		getNamespaceRes  storage.ScmNamespaces
		getStateErr      error
		prepErr          error
		startState       storage.ScmState
		expEndState      storage.ScmState
		expResponse      *storage.ScmPrepareResponse
	}{
		"init scan fails": {
			discoverErr: FaultDiscoveryFailed,
		},
		"noop": {
			expResponse: &storage.ScmPrepareResponse{
				RebootRequired: false,
			},
		},
		"should reboot after prep": {
			shouldReboot: true,
			startState:   storage.ScmStateNoRegions,
			expEndState:  storage.ScmStateFreeCapacity,
			expResponse: &storage.ScmPrepareResponse{
				State:          storage.ScmStateFreeCapacity,
				RebootRequired: true,
			},
		},
		"should reboot after reset": {
			reset:        true,
			shouldReboot: true,
			startState:   storage.ScmStateNoCapacity,
			expEndState:  storage.ScmStateNoRegions,
			expResponse: &storage.ScmPrepareResponse{
				State:          storage.ScmStateNoRegions,
				RebootRequired: true,
			},
		},
		"prep with ndctl missing": {
			getNamespaceErr: FaultMissingNdctl,
		},
		"prep succeeds, update fails": {
			startInitialized: true,
			startState:       storage.ScmStateNoCapacity,
			expEndState:      storage.ScmStateNoRegions,
			getStateErr:      errors.New("update failed"),
		},
		"prep fails": {
			startState:  storage.ScmStateNoCapacity,
			expEndState: storage.ScmStateNoRegions,
			prepErr:     errors.New("prep failed"),
		},
		"reset with ndctl missing": {
			reset:           true,
			getNamespaceErr: FaultMissingNdctl,
		},
		"reset succeeds, update fails": {
			reset:            true,
			startInitialized: true,
			startState:       storage.ScmStateNoCapacity,
			expEndState:      storage.ScmStateNoRegions,
			getStateErr:      errors.New("update failed"),
		},
		"reset fails": {
			reset:       true,
			startState:  storage.ScmStateNoCapacity,
			expEndState: storage.ScmStateNoRegions,
			prepErr:     errors.New("prep reset failed"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.getNamespaceRes == nil {
				tc.getNamespaceRes = storage.ScmNamespaces{defaultNamespace}
			}
			mbc := &MockBackendConfig{
				DiscoverErr:         tc.discoverErr,
				DiscoverRes:         storage.ScmModules{defaultModule},
				GetPmemNamespaceRes: tc.getNamespaceRes,
				GetPmemNamespaceErr: tc.getNamespaceErr,
				GetPmemStateErr:     tc.getStateErr,
				StartingState:       tc.startState,
				NextState:           tc.expEndState,
				PrepNeedsReboot:     tc.shouldReboot,
				PrepErr:             tc.prepErr,
			}
			p := NewMockProvider(log, mbc, nil)

			if tc.startInitialized {
				p.scanCompleted = true
				p.modules = mbc.DiscoverRes
				p.namespaces = tc.getNamespaceRes
			}
			cmpRes := func(t *testing.T, want, got *storage.ScmPrepareResponse) {
				t.Helper()
				if diff := cmp.Diff(want, got); diff != "" {
					t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
				}
			}

			res, err := p.Prepare(storage.ScmPrepareRequest{Reset: tc.reset})
			if err != nil {
				switch err {
				case FaultMissingNdctl:
					cmpRes(t, tc.expResponse, res)
					return
				case tc.discoverErr, tc.getStateErr, tc.prepErr:
					return
				default:
					t.Fatal(err)
				}
			}
			cmpRes(t, tc.expResponse, res)
		})
	}
}

func TestProvider_GetPmemState(t *testing.T) {
	for name, tc := range map[string]struct {
		startInitialized bool
		discoverErr      error
		getNamespaceErr  error
		startState       storage.ScmState
		expEndState      storage.ScmState
	}{
		"init scan fails": {
			discoverErr: FaultDiscoveryFailed,
		},
		"ndctl missing": {
			getNamespaceErr: FaultMissingNdctl,
		},
		"ok": {
			startState:  storage.ScmStateNoCapacity,
			expEndState: storage.ScmStateNoCapacity,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			mbc := &MockBackendConfig{
				DiscoverErr:         tc.discoverErr,
				DiscoverRes:         storage.ScmModules{defaultModule},
				GetPmemNamespaceErr: tc.getNamespaceErr,
				StartingState:       tc.startState,
				NextState:           tc.expEndState,
			}
			p := NewMockProvider(log, mbc, nil)
			p.scanCompleted = tc.startInitialized
			cmpRes := func(t *testing.T, want, got storage.ScmState) {
				t.Helper()
				if diff := cmp.Diff(want, got); diff != "" {
					t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
				}
			}

			res, err := p.GetPmemState()
			if err != nil {
				switch err {
				case tc.discoverErr, tc.getNamespaceErr:
					return
				default:
					t.Fatal(err)
				}
			}
			cmpRes(t, tc.expEndState, res)
		})
	}
}

func TestProvider_CheckFormat(t *testing.T) {
	const (
		goodMountPoint = "/mnt/daos"
		goodDevice     = "/dev/pmem0"
	)

	for name, tc := range map[string]struct {
		mountPoint      string
		discoverErr     error
		getNamespaceErr error
		alreadyMounted  bool
		isMountedErr    error
		getFsStr        string
		getFsErr        error
		request         *storage.ScmFormatRequest
		expResponse     *storage.ScmFormatResponse
		expErr          error
	}{
		"init scan fails (dcpm)": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DcpmParams{
					Device: goodDevice,
				},
			},
			discoverErr: FaultDiscoveryFailed,
		},
		"missing ndctl (dcpm)": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DcpmParams{
					Device: goodDevice,
				},
			},
			getNamespaceErr: FaultMissingNdctl,
		},
		"missing mount point": {
			mountPoint: "",
			expErr:     FaultFormatMissingMountpoint,
		},
		"conflicting config": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Ramdisk: &storage.RamdiskParams{
					Size: 1,
				},
				Dcpm: &storage.DcpmParams{
					Device: goodDevice,
				},
			},
			expErr: FaultFormatConflictingParam,
		},
		"missing dcpm device": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm:       &storage.DcpmParams{},
			},
			expErr: FaultFormatInvalidDeviceCount,
		},
		"missing source config": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
			},
			expErr: FaultFormatMissingParam,
		},
		"isMounted fails": {
			mountPoint:   goodMountPoint,
			isMountedErr: errors.New("is mounted check failed"),
		},
		"mountpoint doesn't exist": {
			mountPoint:   goodMountPoint,
			isMountedErr: os.ErrNotExist,
			expResponse: &storage.ScmFormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  false,
			},
		},
		"already mounted": {
			mountPoint:     goodMountPoint,
			alreadyMounted: true,
			expResponse: &storage.ScmFormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
				Mounted:    true,
			},
		},
		"getFs fails": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DcpmParams{
					Device: goodDevice,
				},
			},
			getFsErr: errors.New("getfs failed"),
		},
		"already formatted; not mountable": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: "reiserfs",
			expResponse: &storage.ScmFormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
			},
		},
		"already formatted; mountable": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: fsTypeExt4,
			expResponse: &storage.ScmFormatResponse{
				Mountpoint: goodMountPoint,
				Mountable:  true,
				Formatted:  true,
			},
		},
		"not formatted": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: fsTypeNone,
			expResponse: &storage.ScmFormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  false,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			mbc := &MockBackendConfig{
				DiscoverErr:         tc.discoverErr,
				DiscoverRes:         storage.ScmModules{defaultModule},
				GetPmemNamespaceErr: tc.getNamespaceErr,
			}
			msc := &MockSysConfig{
				IsMountedBool: tc.alreadyMounted,
				IsMountedErr:  tc.isMountedErr,
				GetfsStr:      tc.getFsStr,
				GetfsErr:      tc.getFsErr,
			}
			p := NewMockProvider(log, mbc, msc)
			cmpRes := func(t *testing.T, want, got *storage.ScmFormatResponse) {
				t.Helper()
				if diff := cmp.Diff(want, got); diff != "" {
					t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
				}
			}

			req := tc.request
			if req == nil {
				req = &storage.ScmFormatRequest{
					Mountpoint: tc.mountPoint,
					Ramdisk: &storage.RamdiskParams{
						Size: 1,
					},
				}
			}
			res, err := p.CheckFormat(*req)
			if err != nil {
				switch errors.Cause(err) {
				case tc.discoverErr, tc.getNamespaceErr, tc.isMountedErr, tc.getFsErr,
					tc.expErr:
					return
				default:
					t.Fatal(err)
				}
			}
			cmpRes(t, tc.expResponse, res)
		})
	}
}

func TestProvider_makeMountPath(t *testing.T) {
	testDir, err := ioutil.TempDir("", strings.Replace(t.Name(), "/", "-", -1))
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(testDir)

	dir1 := "/fake"
	dir2 := "/nested"
	dir3 := "/mountpoint"
	nestedMount := dir1 + dir2 + dir3

	for name, tc := range map[string]struct {
		mntpt     string
		existPath string
		statErrs  map[string]error
		expCreate bool
		expErr    error
	}{
		"existing nested": {
			mntpt: nestedMount,
		},
		"existing nested; bad perms": {
			mntpt: nestedMount,
			statErrs: map[string]error{
				nestedMount: os.ErrPermission,
			},
			expErr: os.ErrPermission,
		},
		"new nested": {
			mntpt: nestedMount,
			statErrs: map[string]error{
				dir1:        os.ErrNotExist,
				dir1 + dir2: os.ErrNotExist,
				nestedMount: os.ErrNotExist,
			},
			expCreate: true,
		},
		"partial existing nested": {
			mntpt:     nestedMount,
			existPath: dir1,
			statErrs: map[string]error{
				dir1 + dir2: os.ErrNotExist,
				nestedMount: os.ErrNotExist,
			},
			expCreate: true,
		},
		// similate situation where mount ancestor dir exists with
		// incompatible permissions
		"partial existing nested; bad perms": {
			mntpt:     nestedMount,
			existPath: dir1 + dir2,
			statErrs: map[string]error{
				dir1 + dir2: os.ErrPermission,
				nestedMount: os.ErrNotExist,
			},
			expErr: os.ErrPermission,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			testCaseDir := filepath.Join(testDir, "tc")
			if err := os.Mkdir(testCaseDir, defaultMountPointPerms); err != nil {
				t.Fatal(err)
			}
			defer os.RemoveAll(testCaseDir)

			if tc.existPath != "" {
				// when simulating full or partial mountpoint
				// path, create the existing directory structure
				// in the test case temporary directory
				ep := filepath.Join(testCaseDir, tc.existPath)
				if err := os.MkdirAll(ep, defaultMountPointPerms); err != nil {
					t.Fatal(err)
				}
			}

			msc := MockSysConfig{
				statErrors: make(map[string]error),
			}
			for mp, err := range tc.statErrs {
				// mocked stat return errors updated for paths
				// relative to the test case temporary directory
				k := filepath.Join(testCaseDir, mp)
				msc.statErrors[k] = err
			}
			p := NewMockProvider(log, nil, &msc)

			tMntpt := filepath.Join(testCaseDir, tc.mntpt)

			gotErr := p.makeMountPath(tMntpt, os.Getuid(), os.Getgid())
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil || tc.expCreate == false {
				return
			}

			// verify that the expected directory structure has been
			// created within the test case temporary directory
			if _, err := os.Stat(tMntpt); err != nil {
				t.Fatalf("Mount point not accessible: %s", err)
			}
		})
	}
}

func TestProvider_Format(t *testing.T) {
	const (
		goodMountPoint     = "/mnt/daos"
		nestedMountPoint   = "/mnt/daos/0"
		relativeMountPoint = "mnt/daos/0"
		badMountPoint      = "/this/should/not/work"
		goodDevice         = "/dev/pmem0"
	)

	// NB: Some overlap here between this and CheckFormat tests,
	// but tests are cheap and this encourages good coverage.
	for name, tc := range map[string]struct {
		mountPoint      string
		discoverErr     error
		getNamespaceErr error
		alreadyMounted  bool
		isMountedErr    error
		getFsStr        string
		getFsErr        error
		mountErr        error
		unmountErr      error
		mkfsErr         error
		request         *storage.ScmFormatRequest
		expResponse     *storage.ScmFormatResponse
		expErr          error
	}{
		"init scan fails (dcpm)": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DcpmParams{
					Device: goodDevice,
				},
			},
			discoverErr: FaultDiscoveryFailed,
		},
		"missing ndctl (dcpm)": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DcpmParams{
					Device: goodDevice,
				},
			},
			getNamespaceErr: FaultMissingNdctl,
		},
		"missing mount point": {
			mountPoint: "",
			expErr:     FaultFormatMissingMountpoint,
		},
		"conflicting config": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Ramdisk: &storage.RamdiskParams{
					Size: 1,
				},
				Dcpm: &storage.DcpmParams{
					Device: goodDevice,
				},
			},
			expErr: FaultFormatConflictingParam,
		},
		"missing dcpm device": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm:       &storage.DcpmParams{},
			},
			expErr: FaultFormatInvalidDeviceCount,
		},
		"missing source config": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
			},
			expErr: FaultFormatMissingParam,
		},
		"isMounted fails": {
			mountPoint:   goodMountPoint,
			isMountedErr: errors.New("is mounted check failed"),
		},
		"ramdisk: mountpoint doesn't exist": {
			mountPoint:   goodMountPoint,
			isMountedErr: os.ErrNotExist,
			expResponse: &storage.ScmFormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
				Mounted:    true,
			},
		},
		"ramdisk: mountpoint not accessible": {
			mountPoint:   nestedMountPoint,
			isMountedErr: os.ErrPermission,
			expResponse: &storage.ScmFormatResponse{
				Mountpoint: nestedMountPoint,
				Formatted:  true,
				Mounted:    true,
			},
			expErr: FaultPathAccessDenied(nestedMountPoint),
		},
		"ramdisk: already mounted, no reformat": {
			mountPoint:     goodMountPoint,
			alreadyMounted: true,
			expErr:         FaultFormatNoReformat,
		},
		"ramdisk: not mounted": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Ramdisk: &storage.RamdiskParams{
					Size: 1,
				},
			},
			expResponse: &storage.ScmFormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
				Mounted:    true,
			},
		},
		"ramdisk: not mounted; mkdir fails": {
			request: &storage.ScmFormatRequest{
				Mountpoint: badMountPoint,
				Ramdisk: &storage.RamdiskParams{
					Size: 1,
				},
			},
			expErr: fmt.Errorf("mkdir /%s: permission denied",
				strings.Split(badMountPoint, "/")[1]),
		},
		"ramdisk: already mounted; reformat": {
			request: &storage.ScmFormatRequest{
				Force:      true,
				Mountpoint: goodMountPoint,
				Ramdisk: &storage.RamdiskParams{
					Size: 1,
				},
			},
			alreadyMounted: true,
			expResponse: &storage.ScmFormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
				Mounted:    true,
			},
		},
		"ramdisk: already mounted; reformat; unmount fails": {
			request: &storage.ScmFormatRequest{
				Force:      true,
				Mountpoint: goodMountPoint,
				Ramdisk: &storage.RamdiskParams{
					Size: 1,
				},
			},
			alreadyMounted: true,
			unmountErr:     errors.New("unmount failed"),
		},
		"ramdisk: already mounted; reformat; mount fails": {
			request: &storage.ScmFormatRequest{
				Force:      true,
				Mountpoint: goodMountPoint,
				Ramdisk: &storage.RamdiskParams{
					Size: 1,
				},
			},
			alreadyMounted: true,
			mountErr:       errors.New("mount failed"),
		},
		"ramdisk: mountpoint doesn't exist; nested mountpoint": {
			mountPoint:   nestedMountPoint,
			isMountedErr: os.ErrNotExist,
			expResponse: &storage.ScmFormatResponse{
				Mountpoint: nestedMountPoint,
				Formatted:  true,
				Mounted:    true,
			},
		},
		"dcpm: getFs fails": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DcpmParams{
					Device: goodDevice,
				},
			},
			getFsErr: errors.New("getfs failed"),
		},
		"dcpm: mountpoint doesn't exist; already formatted; no reformat": {
			isMountedErr: os.ErrNotExist,
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: "reiserfs",
			expErr:   FaultFormatNoReformat,
		},
		"dcpm: mountpoint not accessible": {
			isMountedErr: os.ErrPermission,
			request: &storage.ScmFormatRequest{
				Mountpoint: nestedMountPoint,
				Dcpm: &storage.DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: "reiserfs",
			expErr:   FaultPathAccessDenied(nestedMountPoint),
		},
		"dcpm: not mounted; already formatted; no reformat": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: "reiserfs",
			expErr:   FaultFormatNoReformat,
		},
		"dcpm: mountpoint doesn't exist; already formatted; reformat": {
			isMountedErr: os.ErrNotExist,
			request: &storage.ScmFormatRequest{
				Force:      true,
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: "reiserfs",
			expResponse: &storage.ScmFormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
				Mounted:    true,
			},
		},
		"dcpm: not mounted; already formatted; reformat": {
			request: &storage.ScmFormatRequest{
				Force:      true,
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: "reiserfs",
			expResponse: &storage.ScmFormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
				Mounted:    true,
			},
		},
		"dcpm: mounted; already formatted; reformat": {
			request: &storage.ScmFormatRequest{
				Force:      true,
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: "reiserfs",
			expResponse: &storage.ScmFormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
				Mounted:    true,
			},
		},
		"dcpm: mountpoint doesn't exist; not formatted": {
			isMountedErr: os.ErrNotExist,
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: fsTypeNone,
			expResponse: &storage.ScmFormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
				Mounted:    true,
			},
		},
		"dcpm: not mounted; not formatted": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: fsTypeNone,
			expResponse: &storage.ScmFormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
				Mounted:    true,
			},
		},
		"dcpm: not mounted; not formatted; mkfs fails": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: fsTypeNone,
			mkfsErr:  errors.New("mkfs failed"),
		},
		"dcpm: not mounted; not formatted; mount fails": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: fsTypeNone,
			mountErr: errors.New("mount failed"),
		},
		"dcpm: missing device": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DcpmParams{
					Device: "/bad/device",
				},
			},
			getFsErr: &os.PathError{
				Op:   "stat",
				Path: "/bad/device",
				Err:  os.ErrNotExist,
			},
			expErr: FaultFormatMissingDevice("/bad/device"),
		},
		"dcpm: mountpoint doesn't exist; not formatted; nested mountpoint": {
			isMountedErr: os.ErrNotExist,
			request: &storage.ScmFormatRequest{
				Mountpoint: nestedMountPoint,
				Dcpm: &storage.DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: fsTypeNone,
			expResponse: &storage.ScmFormatResponse{
				Mountpoint: nestedMountPoint,
				Formatted:  true,
				Mounted:    true,
			},
		},
		"dcpm: not mounted; not formatted; nested mountpoint": {
			request: &storage.ScmFormatRequest{
				Mountpoint: nestedMountPoint,
				Dcpm: &storage.DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: fsTypeNone,
			expResponse: &storage.ScmFormatResponse{
				Mountpoint: nestedMountPoint,
				Formatted:  true,
				Mounted:    true,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			testDir, clean := common.CreateTestDir(t)
			defer clean()

			mbc := &MockBackendConfig{
				DiscoverErr:         tc.discoverErr,
				DiscoverRes:         storage.ScmModules{defaultModule},
				GetPmemNamespaceErr: tc.getNamespaceErr,
			}
			msc := &MockSysConfig{
				IsMountedBool: tc.alreadyMounted,
				IsMountedErr:  tc.isMountedErr,
				GetfsStr:      tc.getFsStr,
				GetfsErr:      tc.getFsErr,
				MkfsErr:       tc.mkfsErr,
				MountErr:      tc.mountErr,
				UnmountErr:    tc.unmountErr,
			}
			p := NewMockProvider(log, mbc, msc)
			cmpRes := func(t *testing.T, want, got *storage.ScmFormatResponse) {
				t.Helper()
				if diff := cmp.Diff(want, got); diff != "" {
					t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
				}
			}

			req := tc.request
			if req == nil {
				req = &storage.ScmFormatRequest{
					Mountpoint: tc.mountPoint,
					Ramdisk: &storage.RamdiskParams{
						Size: 1,
					},
				}
			}
			if req.OwnerUID == 0 {
				req.OwnerUID = os.Getuid()
			}
			if req.OwnerGID == 0 {
				req.OwnerGID = os.Getgid()
			}
			if req.Mountpoint != "" && req.Mountpoint != badMountPoint {
				req.Mountpoint = filepath.Join(testDir, req.Mountpoint)
			}
			if tc.expResponse != nil {
				tc.expResponse.Mountpoint = filepath.Join(testDir, tc.expResponse.Mountpoint)
			}

			res, err := p.Format(*req)
			if err != nil {
				switch errors.Cause(err) {
				case tc.discoverErr, tc.getNamespaceErr, tc.getFsErr,
					tc.mkfsErr, tc.mountErr, tc.unmountErr, tc.expErr:
					return
				case tc.isMountedErr:
					if tc.isMountedErr != os.ErrNotExist {
						return
					}
					t.Fatalf("%s leaked from IsMounted() check", err)
				default:
					if tc.expErr == nil {
						t.Fatal(err)
					}

					// expErr will contain the original mountpoint
					errStr := errors.Cause(err).Error()
					errStr = strings.Replace(errStr, testDir, "", 1)
					if diff := cmp.Diff(tc.expErr.Error(), errStr); diff != "" {
						t.Fatalf("unexpected error (-want, +got):\n%s\n", diff)
					}

					return
				}
			}
			cmpRes(t, tc.expResponse, res)
		})
	}
}

func TestParseFsType(t *testing.T) {
	for name, tc := range map[string]struct {
		input     string
		expFsType string
		expError  error
	}{
		"not formatted": {
			input:     "/dev/pmem1: data\n",
			expFsType: fsTypeNone,
		},
		"formatted": {
			input:     "/dev/pmem0: Linux rev 1.0 ext4 filesystem data, UUID=09619a0d-0c9e-46b4-add5-faf575dd293d\n",
			expFsType: fsTypeExt4,
		},
		"empty input": {
			expFsType: fsTypeUnknown,
		},
		"mangled short": {
			input:     "/dev/pmem0",
			expFsType: fsTypeUnknown,
		},
		"mangled medium": {
			input:     "/dev/pmem0: Linux",
			expFsType: fsTypeUnknown,
		},
		"mangled long": {
			input:     "/dev/pmem0: Linux quack bark",
			expFsType: fsTypeUnknown,
		},
		"formatted; ext2": {
			input:     "/dev/pmem0: Linux rev 1.0 ext2 filesystem data, UUID=0ce47201-6f25-4569-9e82-34c9d91173bb (large files)\n",
			expFsType: "ext2",
		},
		"garbage in header": {
			input:     "/dev/pmem1: COM executable for DOS",
			expFsType: "DOS",
		},
	} {
		t.Run(name, func(t *testing.T) {
			if diff := cmp.Diff(tc.expFsType, parseFsType(tc.input)); diff != "" {
				t.Fatalf("unexpected fsType (-want, +got):\n%s\n", diff)
			}
		})
	}
}
