//
// (C) Copyright 2019-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package scm

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/provider/system"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/mount"
)

var (
	defaultModule    = mockModule("abcd", 30, 1, 0, 0, 1)
	defaultNamespace = storage.MockScmNamespace()
)

func TestProvider_Scan(t *testing.T) {
	for name, tc := range map[string]struct {
		mbc     *MockBackendConfig
		expErr  error
		expResp *storage.ScmScanResponse
	}{
		"no modules": {
			mbc: &MockBackendConfig{
				GetModulesRes: storage.ScmModules{},
			},
			expResp: &storage.ScmScanResponse{
				Modules:    storage.ScmModules{},
				Namespaces: storage.ScmNamespaces{},
			},
		},
		"no namespaces": {
			mbc: &MockBackendConfig{
				GetModulesRes:    storage.ScmModules{defaultModule},
				GetNamespacesRes: storage.ScmNamespaces{},
			},
			expResp: &storage.ScmScanResponse{
				Modules:    storage.ScmModules{defaultModule},
				Namespaces: storage.ScmNamespaces{},
			},
		},
		"ok": {
			mbc: &MockBackendConfig{
				GetModulesRes:    storage.ScmModules{defaultModule},
				GetNamespacesRes: storage.ScmNamespaces{defaultNamespace},
			},
			expResp: &storage.ScmScanResponse{
				Modules:    storage.ScmModules{defaultModule},
				Namespaces: storage.ScmNamespaces{defaultNamespace},
			},
		},
		"get modules fails": {
			mbc: &MockBackendConfig{
				GetModulesErr: FaultGetModulesFailed,
			},
			expErr: FaultGetModulesFailed,
		},
		"get namespaces fails": {
			mbc: &MockBackendConfig{
				GetModulesRes:    storage.ScmModules{defaultModule},
				GetNamespacesErr: errors.New("get namespaces failed"),
			},
			expErr: errors.New("get namespaces failed"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			p := NewMockProvider(log, tc.mbc, nil)

			resp, err := p.Scan(storage.ScmScanRequest{})
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, resp); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestProvider_Prepare(t *testing.T) {
	cmpRes := func(t *testing.T, want, got *storage.ScmPrepareResponse) {
		t.Helper()
		if diff := cmp.Diff(want, got); diff != "" {
			t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
		}
	}

	for name, tc := range map[string]struct {
		reset    bool
		mbc      *MockBackendConfig
		scanErr  error
		scanResp *storage.ScmScanResponse
		expErr   error
		expResp  *storage.ScmPrepareResponse
	}{
		"scan fails": {
			scanResp: &storage.ScmScanResponse{},
			scanErr:  FaultGetModulesFailed,
			expErr:   FaultGetModulesFailed,
		},
		"prep fails": {
			scanResp: &storage.ScmScanResponse{
				Modules: storage.ScmModules{defaultModule},
			},
			mbc: &MockBackendConfig{
				PrepErr: errors.New("fail"),
			},
			expErr: errors.New("fail"),
		},
		"prep succeeds": {
			scanResp: &storage.ScmScanResponse{
				Modules: storage.ScmModules{defaultModule},
			},
			mbc: &MockBackendConfig{
				PrepRes: &storage.ScmPrepareResponse{
					Socket: &storage.ScmSocketState{
						State: storage.ScmNoFreeCap,
					},
					Namespaces:     storage.ScmNamespaces{defaultNamespace},
					RebootRequired: true,
				},
			},
			expResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmNoFreeCap,
				},
				Namespaces:     storage.ScmNamespaces{defaultNamespace},
				RebootRequired: true,
			},
		},
		"reset fails": {
			reset: true,
			scanResp: &storage.ScmScanResponse{
				Modules: storage.ScmModules{defaultModule},
			},
			mbc: &MockBackendConfig{
				PrepResetErr: errors.New("fail"),
			},
			expErr: errors.New("fail"),
		},
		"reset; no namespaces": {
			reset: true,
			scanResp: &storage.ScmScanResponse{
				Modules: storage.ScmModules{defaultModule},
			},
			mbc: &MockBackendConfig{
				PrepResetRes: &storage.ScmPrepareResponse{
					Socket: &storage.ScmSocketState{
						State: storage.ScmFreeCap,
					},
					RebootRequired: true,
				},
			},
			expResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmFreeCap,
				},
				RebootRequired: true,
			},
		},
		"reset; with namespaces": {
			reset: true,
			scanResp: &storage.ScmScanResponse{
				Modules: storage.ScmModules{defaultModule},
				Namespaces: storage.ScmNamespaces{
					defaultNamespace,
					storage.MockScmNamespace(1),
				},
			},
			mbc: &MockBackendConfig{
				PrepResetRes: &storage.ScmPrepareResponse{
					Socket: &storage.ScmSocketState{
						State: storage.ScmNoFreeCap,
					},
					RebootRequired: true,
				},
			},
			expResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmNoFreeCap,
				},
				RebootRequired: true,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			p := NewMockProvider(log, tc.mbc, nil)
			mockSys := p.sys.(*system.MockSysProvider)
			p.mounter = mount.NewProvider(log, mockSys)

			for _, ns := range tc.scanResp.Namespaces {
				if err := mockSys.Mount("", "/dev/"+ns.BlockDevice, "", 0, ""); err != nil {
					t.Fatal(err)
				}
			}

			mockScan := func(storage.ScmScanRequest) (*storage.ScmScanResponse, error) {
				return tc.scanResp, tc.scanErr
			}

			res, err := p.prepare(storage.ScmPrepareRequest{
				Reset: tc.reset,
			}, mockScan)

			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			cmpRes(t, tc.expResp, res)

			// Verify namespaces get unmounted on reset.
			for _, ns := range tc.scanResp.Namespaces {
				isMounted, err := p.IsMounted("/dev/" + ns.BlockDevice)
				if err != nil {
					t.Fatal(err)
				}
				test.AssertEqual(t, !tc.reset, isMounted,
					fmt.Sprintf("unexpected ns %s mounted state, want %v got %v",
						ns.BlockDevice, !tc.reset, isMounted))
			}
		})
	}
}

func TestProvider_CheckFormat(t *testing.T) {
	const (
		goodMountPoint = "/mnt/daos"
		goodDevice     = "/dev/pmem0"
	)

	for name, tc := range map[string]struct {
		mountPoint     string
		alreadyMounted bool
		isMountedErr   error
		getFsStr       string
		getFsErr       error
		request        *storage.ScmFormatRequest
		expResponse    *storage.ScmFormatResponse
		expErr         error
	}{
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
				Dcpm: &storage.DeviceParams{
					Device: goodDevice,
				},
			},
			expErr: FaultFormatConflictingParam,
		},
		"missing dcpm device": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm:       &storage.DeviceParams{},
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
			expErr:       errors.New("is mounted check failed"),
		},
		"mountpoint doesn't exist": {
			mountPoint:   goodMountPoint,
			isMountedErr: os.ErrNotExist,
			expResponse: &storage.ScmFormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  false,
			},
		},
		"mountpoint doesn't exist; dcpm has expected fs": {
			request: &storage.ScmFormatRequest{
				Mountpoint: "/missing/dir",
				Dcpm: &storage.DeviceParams{
					Device: goodDevice,
				},
			},
			getFsStr:     system.FsTypeExt4,
			isMountedErr: os.ErrNotExist,
			expErr: storage.FaultDeviceWithFsNoMountpoint(goodDevice,
				"/missing/dir"),
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
				Dcpm: &storage.DeviceParams{
					Device: goodDevice,
				},
			},
			getFsErr: errors.New("getfs failed"),
			expErr:   errors.New("getfs failed"),
		},
		"already formatted; not mountable": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DeviceParams{
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
				Dcpm: &storage.DeviceParams{
					Device: goodDevice,
				},
			},
			getFsStr: system.FsTypeExt4,
			expResponse: &storage.ScmFormatResponse{
				Mountpoint: goodMountPoint,
				Mountable:  true,
				Formatted:  true,
			},
		},
		"not formatted": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DeviceParams{
					Device: goodDevice,
				},
			},
			getFsStr: system.FsTypeNone,
			expResponse: &storage.ScmFormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  false,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			msc := &system.MockSysConfig{
				IsMountedBool: tc.alreadyMounted,
				IsMountedErr:  tc.isMountedErr,
				GetfsStr:      tc.getFsStr,
				GetfsErr:      tc.getFsErr,
			}

			p := NewMockProvider(log, nil, msc)
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
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			cmpRes(t, tc.expResponse, res)
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
		mountPoint         string
		alreadyMounted     bool
		isMountedErr       error
		getFsStr           string
		getFsErr           error
		mountErr           error
		unmountErr         error
		mkfsErr            error
		chownErr           error
		makeMountpointErr  error
		clearMountpointErr error
		request            *storage.ScmFormatRequest
		expResponse        *storage.ScmFormatResponse
		expMountOpts       string
		expErr             error
	}{
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
				Dcpm: &storage.DeviceParams{
					Device: goodDevice,
				},
			},
			expErr: FaultFormatConflictingParam,
		},
		"missing dcpm device": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm:       &storage.DeviceParams{},
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
			expErr: storage.FaultPathAccessDenied(nestedMountPoint),
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
			expMountOpts: "mpol=prefer:0,size=1g,huge=always",
		},
		"ramdisk: hugepages disabled": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Ramdisk: &storage.RamdiskParams{
					Size:             1,
					DisableHugepages: true,
				},
			},
			expResponse: &storage.ScmFormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
				Mounted:    true,
			},
			expMountOpts: "mpol=prefer:0,size=1g",
		},
		"ramdisk: not mounted; mkdir fails": {
			request: &storage.ScmFormatRequest{
				Mountpoint: badMountPoint,
				Ramdisk: &storage.RamdiskParams{
					Size: 1,
				},
			},
			makeMountpointErr: errors.New("mock MakeMountpoint"),
			expErr:            errors.New("mock MakeMountpoint"),
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
		"ramdisk: already mounted; reformat; clear fails": {
			request: &storage.ScmFormatRequest{
				Force:      true,
				Mountpoint: goodMountPoint,
				Ramdisk: &storage.RamdiskParams{
					Size: 1,
				},
			},
			alreadyMounted:     true,
			clearMountpointErr: errors.New("mock ClearMountpoint"),
			expErr:             errors.New("mock ClearMountpoint"),
		},
		"ramdisk: format succeeds, chown fails": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Ramdisk: &storage.RamdiskParams{
					Size: 1,
				},
			},
			chownErr: errors.New("chown failed"),
			expErr:   errors.New("chown failed"),
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
				Dcpm: &storage.DeviceParams{
					Device: goodDevice,
				},
			},
			getFsErr: errors.New("getfs failed"),
		},
		"dcpm: mountpoint doesn't exist; already formatted; no reformat": {
			isMountedErr: os.ErrNotExist,
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DeviceParams{
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
				Dcpm: &storage.DeviceParams{
					Device: goodDevice,
				},
			},
			getFsStr: "reiserfs",
			expErr:   storage.FaultPathAccessDenied(nestedMountPoint),
		},
		"dcpm: not mounted; already formatted; no reformat": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DeviceParams{
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
				Dcpm: &storage.DeviceParams{
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
				Dcpm: &storage.DeviceParams{
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
				Dcpm: &storage.DeviceParams{
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
				Dcpm: &storage.DeviceParams{
					Device: goodDevice,
				},
			},
			getFsStr: system.FsTypeNone,
			expResponse: &storage.ScmFormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
				Mounted:    true,
			},
		},
		"dcpm: not mounted; not formatted": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DeviceParams{
					Device: goodDevice,
				},
			},
			getFsStr: system.FsTypeNone,
			expResponse: &storage.ScmFormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
				Mounted:    true,
			},
			expMountOpts: dcpmMountOpts,
		},
		"dcpm: not mounted; not formatted; mkfs fails": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DeviceParams{
					Device: goodDevice,
				},
			},
			getFsStr: system.FsTypeNone,
			mkfsErr:  errors.New("mkfs failed"),
		},
		"dcpm: not mounted; not formatted; mount fails": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DeviceParams{
					Device: goodDevice,
				},
			},
			getFsStr: system.FsTypeNone,
			mountErr: errors.New("mount failed"),
		},
		"dcpm: format succeeds, chmod fails": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DeviceParams{
					Device: goodDevice,
				},
			},
			getFsStr: system.FsTypeNone,
			chownErr: errors.New("chown failed"),
			expErr:   errors.New("chown failed"),
		},
		"dcpm: missing device": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DeviceParams{
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
				Dcpm: &storage.DeviceParams{
					Device: goodDevice,
				},
			},
			getFsStr: system.FsTypeNone,
			expResponse: &storage.ScmFormatResponse{
				Mountpoint: nestedMountPoint,
				Formatted:  true,
				Mounted:    true,
			},
		},
		"dcpm: not mounted; not formatted; nested mountpoint": {
			request: &storage.ScmFormatRequest{
				Mountpoint: nestedMountPoint,
				Dcpm: &storage.DeviceParams{
					Device: goodDevice,
				},
			},
			getFsStr: system.FsTypeNone,
			expResponse: &storage.ScmFormatResponse{
				Mountpoint: nestedMountPoint,
				Formatted:  true,
				Mounted:    true,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			testDir, clean := test.CreateTestDir(t)
			defer clean()

			msc := &system.MockSysConfig{
				GetfsStr: tc.getFsStr,
				GetfsErr: tc.getFsErr,
				MkfsErr:  tc.mkfsErr,
				ChownErr: tc.chownErr,
			}

			mmc := &storage.MockMountProviderConfig{
				IsMountedRes:       tc.alreadyMounted,
				IsMountedErr:       tc.isMountedErr,
				MountErr:           tc.mountErr,
				UnmountErr:         tc.unmountErr,
				MakeMountPathErr:   tc.makeMountpointErr,
				ClearMountpointErr: tc.clearMountpointErr,
			}

			p := NewMockProvider(log, nil, msc)
			p.mounter = storage.NewMockMountProvider(mmc)
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
				case tc.getFsErr, tc.mkfsErr, tc.mountErr, tc.unmountErr, tc.expErr:
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

			if tc.expMountOpts != "" {
				mmp, ok := p.mounter.(*storage.MockMountProvider)
				if ok {
					gotOpts, _ := mmp.GetMountOpts(req.Mountpoint)
					if diff := cmp.Diff(tc.expMountOpts, gotOpts); diff != "" {
						t.Fatalf("unexpected mount options (-want, +got):\n%s\n", diff)
					}
				}
			}
		})
	}
}
