//
// (C) Copyright 2019-2020 Intel Corporation.
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

func TestProviderScan(t *testing.T) {
	for name, tc := range map[string]struct {
		rescan          bool
		discoverErr     error
		discoverRes     storage.ScmModules
		getNamespaceErr error
		getNamespaceRes storage.ScmNamespaces
		getStateErr     error
		expResponse     *ScanResponse
	}{
		"no modules": {
			discoverRes: storage.ScmModules{},
			expResponse: &ScanResponse{
				Modules: storage.ScmModules{},
			},
		},
		"no namespaces": {
			discoverRes:     storage.ScmModules{defaultModule},
			getNamespaceRes: storage.ScmNamespaces{},
			expResponse: &ScanResponse{
				Modules:    storage.ScmModules{defaultModule},
				Namespaces: storage.ScmNamespaces{},
			},
		},
		"ok": {
			expResponse: &ScanResponse{
				Modules:    storage.ScmModules{defaultModule},
				Namespaces: storage.ScmNamespaces{defaultNamespace},
			},
		},
		"rescan": {
			rescan: true,
			expResponse: &ScanResponse{
				Modules:    storage.ScmModules{defaultModule},
				Namespaces: storage.ScmNamespaces{defaultNamespace},
			},
		},
		"ndctl missing": {
			getNamespaceErr: FaultMissingNdctl,
			expResponse: &ScanResponse{
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
			cmpRes := func(t *testing.T, want, got *ScanResponse) {
				t.Helper()
				if diff := cmp.Diff(want, got); diff != "" {
					t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
				}
			}

			res, err := p.Scan(ScanRequest{})
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
			res, err = p.Scan(ScanRequest{Rescan: tc.rescan})
			if err != nil {
				t.Fatal(err)
			}
			cmpRes(t, tc.expResponse, res)
		})
	}
}

func TestProviderPrepare(t *testing.T) {
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
		expResponse      *PrepareResponse
	}{
		"init scan fails": {
			discoverErr: FaultDiscoveryFailed,
		},
		"noop": {
			expResponse: &PrepareResponse{
				RebootRequired: false,
			},
		},
		"should reboot after prep": {
			shouldReboot: true,
			startState:   storage.ScmStateNoRegions,
			expEndState:  storage.ScmStateFreeCapacity,
			expResponse: &PrepareResponse{
				State:          storage.ScmStateFreeCapacity,
				RebootRequired: true,
			},
		},
		"should reboot after reset": {
			reset:        true,
			shouldReboot: true,
			startState:   storage.ScmStateNoCapacity,
			expEndState:  storage.ScmStateNoRegions,
			expResponse: &PrepareResponse{
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
			cmpRes := func(t *testing.T, want, got *PrepareResponse) {
				t.Helper()
				if diff := cmp.Diff(want, got); diff != "" {
					t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
				}
			}

			res, err := p.Prepare(PrepareRequest{Reset: tc.reset})
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

func TestProviderGetPmemState(t *testing.T) {
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

func TestProviderCheckFormat(t *testing.T) {
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
		request         *FormatRequest
		expResponse     *FormatResponse
		expErr          error
	}{
		"init scan fails": {
			discoverErr: FaultDiscoveryFailed,
		},
		"missing ndctl": {
			getNamespaceErr: FaultMissingNdctl,
		},
		"missing mount point": {
			mountPoint: "",
			expErr:     FaultFormatMissingMountpoint,
		},
		"conflicting config": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Ramdisk: &RamdiskParams{
					Size: 1,
				},
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			expErr: FaultFormatConflictingParam,
		},
		"missing dcpm device": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm:       &DcpmParams{},
			},
			expErr: FaultFormatInvalidDeviceCount,
		},
		"missing source config": {
			request: &FormatRequest{
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
			expResponse: &FormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  false,
			},
		},
		"already mounted": {
			mountPoint:     goodMountPoint,
			alreadyMounted: true,
			expResponse: &FormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
				Mounted:    true,
			},
		},
		"getFs fails": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			getFsErr: errors.New("getfs failed"),
		},
		"already formatted; not mountable": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: "reiserfs",
			expResponse: &FormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
			},
		},
		"already formatted; mountable": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: fsTypeExt4,
			expResponse: &FormatResponse{
				Mountpoint: goodMountPoint,
				Mountable:  true,
				Formatted:  true,
			},
		},
		"not formatted": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: fsTypeNone,
			expResponse: &FormatResponse{
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
			cmpRes := func(t *testing.T, want, got *FormatResponse) {
				t.Helper()
				if diff := cmp.Diff(want, got); diff != "" {
					t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
				}
			}

			req := tc.request
			if req == nil {
				req = &FormatRequest{
					Mountpoint: tc.mountPoint,
					Ramdisk: &RamdiskParams{
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

func TestProviderFormat(t *testing.T) {
	const (
		goodMountPoint = "/mnt/daos"
		badMountPoint  = "/this/should/not/work"
		goodDevice     = "/dev/pmem0"
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
		request         *FormatRequest
		expResponse     *FormatResponse
		expErr          error
	}{
		"init scan fails": {
			discoverErr: FaultDiscoveryFailed,
		},
		"missing ndctl": {
			getNamespaceErr: FaultMissingNdctl,
		},
		"missing mount point": {
			mountPoint: "",
			expErr:     FaultFormatMissingMountpoint,
		},
		"conflicting config": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Ramdisk: &RamdiskParams{
					Size: 1,
				},
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			expErr: FaultFormatConflictingParam,
		},
		"missing dcpm device": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm:       &DcpmParams{},
			},
			expErr: FaultFormatInvalidDeviceCount,
		},
		"missing source config": {
			request: &FormatRequest{
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
			expResponse: &FormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
				Mounted:    true,
			},
		},
		"ramdisk: already mounted, no reformat": {
			mountPoint:     goodMountPoint,
			alreadyMounted: true,
			expErr:         FaultFormatNoReformat,
		},
		"ramdisk: not mounted": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Ramdisk: &RamdiskParams{
					Size: 1,
				},
			},
			expResponse: &FormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
				Mounted:    true,
			},
		},
		"ramdisk: not mounted; mkdir fails": {
			request: &FormatRequest{
				Mountpoint: badMountPoint,
				Ramdisk: &RamdiskParams{
					Size: 1,
				},
			},
			expErr: fmt.Errorf("mkdir /%s: permission denied",
				strings.Split(badMountPoint, "/")[1]),
		},
		"ramdisk: already mounted; reformat": {
			request: &FormatRequest{
				Reformat:   true,
				Mountpoint: goodMountPoint,
				Ramdisk: &RamdiskParams{
					Size: 1,
				},
			},
			alreadyMounted: true,
			expResponse: &FormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
				Mounted:    true,
			},
		},
		"ramdisk: already mounted; reformat; unmount fails": {
			request: &FormatRequest{
				Reformat:   true,
				Mountpoint: goodMountPoint,
				Ramdisk: &RamdiskParams{
					Size: 1,
				},
			},
			alreadyMounted: true,
			unmountErr:     errors.New("unmount failed"),
		},
		"ramdisk: already mounted; reformat; mount fails": {
			request: &FormatRequest{
				Reformat:   true,
				Mountpoint: goodMountPoint,
				Ramdisk: &RamdiskParams{
					Size: 1,
				},
			},
			alreadyMounted: true,
			mountErr:       errors.New("mount failed"),
		},
		"dcpm: getFs fails": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			getFsErr: errors.New("getfs failed"),
		},
		"dcpm: mountpoint doesn't exist; already formatted; no reformat": {
			isMountedErr: os.ErrNotExist,
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: "reiserfs",
			expErr:   FaultFormatNoReformat,
		},
		"dcpm: not mounted; already formatted; no reformat": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: "reiserfs",
			expErr:   FaultFormatNoReformat,
		},
		"dcpm: mountpoint doesn't exist; already formatted; reformat": {
			isMountedErr: os.ErrNotExist,
			request: &FormatRequest{
				Reformat:   true,
				Mountpoint: goodMountPoint,
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: "reiserfs",
			expResponse: &FormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
				Mounted:    true,
			},
		},
		"dcpm: not mounted; already formatted; reformat": {
			request: &FormatRequest{
				Reformat:   true,
				Mountpoint: goodMountPoint,
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: "reiserfs",
			expResponse: &FormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
				Mounted:    true,
			},
		},
		"dcpm: mounted; already formatted; reformat": {
			request: &FormatRequest{
				Reformat:   true,
				Mountpoint: goodMountPoint,
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: "reiserfs",
			expResponse: &FormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
				Mounted:    true,
			},
		},
		"dcpm: mountpoint doesn't exist; not formatted": {
			isMountedErr: os.ErrNotExist,
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: fsTypeNone,
			expResponse: &FormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
				Mounted:    true,
			},
		},
		"dcpm: not mounted; not formatted": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: fsTypeNone,
			expResponse: &FormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
				Mounted:    true,
			},
		},
		"dcpm: not mounted; not formatted; mkfs fails": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: fsTypeNone,
			mkfsErr:  errors.New("mkfs failed"),
		},
		"dcpm: not mounted; not formatted; mount fails": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: fsTypeNone,
			mountErr: errors.New("mount failed"),
		},
		"dcpm: missing device": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &DcpmParams{
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
				MkfsErr:       tc.mkfsErr,
				MountErr:      tc.mountErr,
				UnmountErr:    tc.unmountErr,
			}
			p := NewMockProvider(log, mbc, msc)
			cmpRes := func(t *testing.T, want, got *FormatResponse) {
				t.Helper()
				if diff := cmp.Diff(want, got); diff != "" {
					t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
				}
			}

			testDir, err := ioutil.TempDir("", strings.Replace(t.Name(), "/", "-", -1))
			if err != nil {
				t.Fatal(err)
			}
			defer os.RemoveAll(testDir)

			req := tc.request
			if req == nil {
				req = &FormatRequest{
					Mountpoint: tc.mountPoint,
					Ramdisk: &RamdiskParams{
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
					if tc.expErr != nil && tc.expErr.Error() == errors.Cause(err).Error() {
						return
					}
					t.Fatal(err)
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
			expError: errors.New("unable to determine fs type from \"\""),
		},
		"mangled short": {
			input:    "/dev/pmem0",
			expError: errors.New("unable to determine fs type from \"/dev/pmem0\""),
		},
		"mangled medium": {
			input:    "/dev/pmem0: Linux",
			expError: errors.New("unable to determine fs type from \"/dev/pmem0: Linux\""),
		},
		"mangled long": {
			input:    "/dev/pmem0: Linux quack bark",
			expError: errors.New("unable to determine fs type from \"/dev/pmem0: Linux quack bark\""),
		},
	} {
		t.Run(name, func(t *testing.T) {
			fsType, err := parseFsType(tc.input)

			if err != tc.expError {
				if err != nil && tc.expError != nil {
					if err.Error() == tc.expError.Error() {
						return
					}
				}
				t.Fatal(err)
			}

			if diff := cmp.Diff(tc.expFsType, fsType); diff != "" {
				t.Fatalf("unexpected fsType (-want, +got):\n%s\n", diff)
			}
		})
	}
}
