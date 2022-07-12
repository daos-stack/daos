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

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

var (
	mm               = MockModule(nil)
	defaultModule    = &mm
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
				State: storage.ScmSocketState{
					State: storage.ScmNoModules,
				},
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
				GetStateRes: &storage.ScmSocketState{
					State: storage.ScmNoFreeCap,
				},
			},
			expResp: &storage.ScmScanResponse{
				Modules:    storage.ScmModules{defaultModule},
				Namespaces: storage.ScmNamespaces{defaultNamespace},
				State: storage.ScmSocketState{
					State: storage.ScmNoFreeCap,
				},
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
		"get state fails": {
			mbc: &MockBackendConfig{
				GetModulesRes: storage.ScmModules{defaultModule},
				GetStateErr:   errors.New("get state failed"),
			},
			expErr: errors.New("get state failed"),
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
				State: storage.ScmSocketState{
					State: storage.ScmNoRegions,
				},
			},
			mbc: &MockBackendConfig{
				PrepErr: errors.New("fail"),
			},
			expErr: errors.New("fail"),
		},
		"prep succeeds": {
			scanResp: &storage.ScmScanResponse{
				Modules: storage.ScmModules{defaultModule},
				State: storage.ScmSocketState{
					State: storage.ScmNoRegions,
				},
			},
			mbc: &MockBackendConfig{
				PrepRes: &storage.ScmPrepareResponse{
					State: storage.ScmSocketState{
						State: storage.ScmNoFreeCap,
					},
					Namespaces:     storage.ScmNamespaces{defaultNamespace},
					RebootRequired: true,
				},
			},
			expResp: &storage.ScmPrepareResponse{
				State: storage.ScmSocketState{
					State: storage.ScmNoFreeCap,
				},
				Namespaces:     storage.ScmNamespaces{defaultNamespace},
				RebootRequired: true,
			},
		},
		"reset fails": {
			reset: true,
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmSocketState{
					State: storage.ScmFreeCap,
				},
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
				State: storage.ScmSocketState{
					State: storage.ScmFreeCap,
				},
			},
			mbc: &MockBackendConfig{
				PrepResetRes: &storage.ScmPrepareResponse{
					State: storage.ScmSocketState{
						State: storage.ScmFreeCap,
					},
					RebootRequired: true,
				},
			},
			expResp: &storage.ScmPrepareResponse{
				State: storage.ScmSocketState{
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
				State: storage.ScmSocketState{
					State: storage.ScmNoFreeCap,
				},
			},
			mbc: &MockBackendConfig{
				PrepResetRes: &storage.ScmPrepareResponse{
					State: storage.ScmSocketState{
						State: storage.ScmNoFreeCap,
					},
					RebootRequired: true,
				},
			},
			expResp: &storage.ScmPrepareResponse{
				State: storage.ScmSocketState{
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

			for _, ns := range tc.scanResp.Namespaces {
				if err := p.sys.Mount("", "/dev/"+ns.BlockDevice, "", 0, ""); err != nil {
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
				isMounted, err := p.sys.IsMounted("/dev/" + ns.BlockDevice)
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
			defer test.ShowBufferOnFailure(t, buf)

			msc := &MockSysConfig{
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
			if err != nil {
				switch errors.Cause(err) {
				case tc.isMountedErr, tc.getFsErr,
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
			defer test.ShowBufferOnFailure(t, buf)

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
			test.CmpErr(t, tc.expErr, gotErr)
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
		mountPoint     string
		alreadyMounted bool
		isMountedErr   error
		getFsStr       string
		getFsErr       error
		mountErr       error
		unmountErr     error
		mkfsErr        error
		chmodErr       error
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
		"ramdisk: format succeeds, chmod fails": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Ramdisk: &storage.RamdiskParams{
					Size: 1,
				},
			},
			chmodErr: errors.New("chmod failed"),
			expErr:   errors.New("chmod failed"),
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
		"dcpm: format succeeds, chmod fails": {
			request: &storage.ScmFormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &storage.DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: fsTypeNone,
			chmodErr: errors.New("chmod failed"),
			expErr:   errors.New("chmod failed"),
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
			defer test.ShowBufferOnFailure(t, buf)

			testDir, clean := test.CreateTestDir(t)
			defer clean()

			msc := &MockSysConfig{
				IsMountedBool: tc.alreadyMounted,
				IsMountedErr:  tc.isMountedErr,
				GetfsStr:      tc.getFsStr,
				GetfsErr:      tc.getFsErr,
				MkfsErr:       tc.mkfsErr,
				ChmodErr:      tc.chmodErr,
				MountErr:      tc.mountErr,
				UnmountErr:    tc.unmountErr,
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
