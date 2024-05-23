//
// (C) Copyright 2022-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package mount

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/provider/system"
	"github.com/daos-stack/daos/src/control/server/storage"
)

func TestProvider_Mount(t *testing.T) {
	for name, tc := range map[string]struct {
		nilProv bool
		msc     *system.MockSysConfig
		req     storage.MountRequest
		expResp *storage.MountResponse
		expErr  error
	}{
		"nil": {
			nilProv: true,
			expErr:  errors.New("nil"),
		},
		"IsMounted error": {
			msc: &system.MockSysConfig{
				IsMountedErr: errors.New("mock IsMounted"),
			},
			req: storage.MountRequest{
				Source: "/dev/something",
				Target: "/something",
			},
			expErr: errors.New("mock IsMounted"),
		},
		"already mounted": {
			msc: &system.MockSysConfig{
				IsMountedBool: true,
			},
			req: storage.MountRequest{
				Source: "/dev/something",
				Target: "/something",
			},
			expErr: storage.FaultTargetAlreadyMounted,
		},
		"mount failed": {
			msc: &system.MockSysConfig{
				MountErr: errors.New("mock mount"),
			},
			req: storage.MountRequest{
				Source: "/dev/something",
				Target: "/something",
			},
			expErr: errors.New("mock mount"),
		},
		"chmod failed": {
			msc: &system.MockSysConfig{
				ChmodErr: errors.New("mock chmod"),
			},
			req: storage.MountRequest{
				Source: "/dev/something",
				Target: "/something",
			},
			expErr: errors.New("mock chmod"),
		},
		"success": {
			msc: &system.MockSysConfig{},
			req: storage.MountRequest{
				Source: "/dev/something",
				Target: "/something",
			},
			expResp: &storage.MountResponse{
				Target:  "/something",
				Mounted: true,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			var p *Provider
			if !tc.nilProv {
				p = NewProvider(log, system.NewMockSysProvider(log, tc.msc))
			}

			resp, err := p.Mount(tc.req)

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResp, resp); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestProvider_Unmount(t *testing.T) {
	for name, tc := range map[string]struct {
		nilProv bool
		msc     *system.MockSysConfig
		req     storage.MountRequest
		expResp *storage.MountResponse
		expErr  error
	}{
		"nil": {
			nilProv: true,
			expErr:  errors.New("nil"),
		},
		"unmount failed": {
			msc: &system.MockSysConfig{
				UnmountErr: errors.New("mock unmount"),
			},
			req:    storage.MountRequest{Target: "/something"},
			expErr: errors.New("mock unmount"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			var p *Provider
			if !tc.nilProv {
				p = NewProvider(log, system.NewMockSysProvider(log, tc.msc))
			}

			resp, err := p.Unmount(tc.req)

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResp, resp); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestProvider_IsMounted(t *testing.T) {
	testTarget := "/fake"
	for name, tc := range map[string]struct {
		nilProv   bool
		msc       *system.MockSysConfig
		input     string
		expResult bool
		expErr    error
	}{
		"nil": {
			nilProv: true,
			expErr:  errors.New("nil"),
		},
		"permission error": {
			msc: &system.MockSysConfig{
				IsMountedErr: os.ErrPermission,
			},
			expErr: storage.FaultPathAccessDenied(testTarget),
		},
		"error": {
			msc: &system.MockSysConfig{
				IsMountedErr: errors.New("mock IsMounted"),
			},
			expErr: errors.New("mock IsMounted"),
		},
		"not mounted": {
			msc: &system.MockSysConfig{},
		},
		"mounted": {
			msc: &system.MockSysConfig{
				IsMountedBool: true,
			},
			expResult: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			var p *Provider
			if !tc.nilProv {
				p = NewProvider(log, system.NewMockSysProvider(log, tc.msc))
			}

			result, err := p.IsMounted(testTarget)

			test.CmpErr(t, tc.expErr, err)
			test.AssertEqual(t, tc.expResult, result, "")
		})
	}
}

func TestProvider_ClearMountpoint(t *testing.T) {
	testMountpoint := func(path string) string {
		return filepath.Join(path, "mnt")
	}

	for name, tc := range map[string]struct {
		nilProv bool
		msc     *system.MockSysConfig
		setup   func(t *testing.T, testDir string) func(*testing.T)
		expErr  error
	}{
		"nil": {
			nilProv: true,
			expErr:  errors.New("nil"),
		},
		"IsMounted error": {
			msc: &system.MockSysConfig{
				IsMountedErr: errors.New("mock IsMounted"),
			},
			expErr: errors.New("mock IsMounted"),
		},
		"IsMounted nonexistent": {
			msc: &system.MockSysConfig{
				IsMountedErr: os.ErrNotExist,
			},
		},
		"not mounted": {
			msc: &system.MockSysConfig{},
		},
		"mountpoint dir does not exist": { // error is ignored
			msc: &system.MockSysConfig{},
			setup: func(t *testing.T, testDir string) func(*testing.T) {
				return func(_ *testing.T) {}
			},
		},
		"mountpoint dir bad permissions": { // error is ignored
			msc: &system.MockSysConfig{},
			setup: func(t *testing.T, testDir string) func(*testing.T) {
				t.Helper()
				if err := os.Mkdir(testMountpoint(testDir), 0755); err != nil {
					t.Fatal(err)
				}
				if err := os.Chmod(testDir, 0600); err != nil {
					t.Fatal(err)
				}

				return func(t *testing.T) {
					t.Helper()

					if err := os.Chmod(testDir, 0755); err != nil {
						t.Fatal(err)
					}
				}
			},
			expErr: os.ErrPermission,
		},
		"unmount failed": {
			msc: &system.MockSysConfig{
				IsMountedBool: true,
				UnmountErr:    errors.New("mock unmount"),
			},
			expErr: errors.New("mock unmount"),
		},
		"mounted success": {
			msc: &system.MockSysConfig{
				IsMountedBool: true,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			testDir, cleanupDir := test.CreateTestDir(t)
			defer cleanupDir()

			if tc.msc == nil {
				tc.msc = new(system.MockSysConfig)
			}
			tc.msc.RealRemoveAll = true

			if tc.setup == nil {
				tc.setup = func(t *testing.T, testDir string) func(*testing.T) {
					t.Helper()

					if err := os.Mkdir(testMountpoint(testDir), 0755); err != nil {
						t.Fatal(err)
					}

					return func(_ *testing.T) {}
				}
			}
			teardown := tc.setup(t, testDir)
			defer teardown(t)

			var p *Provider
			if !tc.nilProv {
				p = NewProvider(log, system.NewMockSysProvider(log, tc.msc))
			}

			err := p.ClearMountpoint(testMountpoint(testDir))

			test.CmpErr(t, tc.expErr, err)
		})
	}
}

func TestProvider_MakeMountPath(t *testing.T) {
	testDir, cleanupDir := test.CreateTestDir(t)
	defer cleanupDir()

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

			msc := &system.MockSysConfig{
				StatErrors: make(map[string]error),
				RealMkdir:  true,
			}
			for mp, err := range tc.statErrs {
				// mocked stat return errors updated for paths
				// relative to the test case temporary directory
				k := filepath.Join(testCaseDir, mp)
				msc.StatErrors[k] = err
			}
			p := NewProvider(log, system.NewMockSysProvider(log, msc))

			tMntpt := filepath.Join(testCaseDir, tc.mntpt)

			gotErr := p.MakeMountPath(tMntpt, os.Getuid(), os.Getgid())
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
