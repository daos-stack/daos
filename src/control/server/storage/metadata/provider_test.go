//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package metadata

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

func TestMetadata_Provider_Format(t *testing.T) {
	deviceReq := storage.MetadataFormatRequest{
		RootPath:   "/test_root",
		Device:     "/dev/something",
		DataPath:   "/test_root/data",
		OwnerUID:   100,
		OwnerGID:   200,
		EngineIdxs: []uint{0, 3, 4},
	}
	pathReq := storage.MetadataFormatRequest{
		RootPath:   "/test_root",
		DataPath:   "/test_root/data",
		OwnerUID:   100,
		OwnerGID:   200,
		EngineIdxs: []uint{0, 1},
	}

	for name, tc := range map[string]struct {
		nilProv  bool
		sysCfg   *system.MockSysConfig
		mountCfg *storage.MockMountProviderConfig
		setup    func(*testing.T, string) func()
		req      storage.MetadataFormatRequest
		expErr   error
	}{
		"nil provider": {
			nilProv: true,
			req:     deviceReq,
			expErr:  errors.New("nil"),
		},
		"no root path": {
			req: storage.MetadataFormatRequest{
				Device:   "/dev/something",
				DataPath: "/test_root/data",
				OwnerUID: 100,
				OwnerGID: 200,
			},
			expErr: errors.New("no control metadata root path"),
		},
		"data path not a subdir of root path": {
			req: storage.MetadataFormatRequest{
				RootPath: "/test_root",
				Device:   "/dev/something",
				DataPath: "/test_data",
				OwnerUID: 100,
				OwnerGID: 200,
			},
			expErr: errors.New("not a subdirectory"),
		},
		"GetFsType fails": {
			req: pathReq,
			sysCfg: &system.MockSysConfig{
				GetFsTypeErr: []error{errors.New("mock GetFsType")},
			},
			expErr: errors.New("mock GetFsType"),
		},
		"GetFsType returns nosuid flag": {
			req: pathReq,
			sysCfg: &system.MockSysConfig{
				GetFsTypeRes: &system.FsType{
					Name:   system.FsTypeExt4,
					NoSUID: true,
				},
			},
			expErr: FaultBadFilesystem(&system.FsType{
				Name:   system.FsTypeExt4,
				NoSUID: true,
			}),
		},
		"GetFsType returns nfs": {
			req: pathReq,
			sysCfg: &system.MockSysConfig{
				GetFsTypeRes: &system.FsType{Name: system.FsTypeNfs},
			},
			expErr: FaultBadFilesystem(&system.FsType{Name: system.FsTypeNfs}),
		},
		"GetFsType returns unknown": {
			req: pathReq,
			sysCfg: &system.MockSysConfig{
				GetFsTypeRes: &system.FsType{Name: system.FsTypeUnknown},
			},
		},
		"GetFsType skipped with device": {
			req: deviceReq,
			sysCfg: &system.MockSysConfig{
				GetFsTypeErr: []error{errors.New("mock GetFsType")},
			},
		},
		"GetFsType retries with parent if dir doesn't exist": {
			req: pathReq,
			sysCfg: &system.MockSysConfig{
				GetFsTypeRes: &system.FsType{Name: system.FsTypeExt4},
				GetFsTypeErr: []error{os.ErrNotExist, os.ErrNotExist, nil},
			},
		},
		"ClearMountpoint fails": {
			req: deviceReq,
			mountCfg: &storage.MockMountProviderConfig{
				ClearMountpointErr: errors.New("mock ClearMountpoint"),
			},
			expErr: errors.New("mock ClearMountpoint"),
		},
		"MakeMountPath fails": {
			req: deviceReq,
			mountCfg: &storage.MockMountProviderConfig{
				MakeMountPathErr: errors.New("mock MakeMountPath"),
			},
			expErr: errors.New("mock MakeMountPath"),
		},
		"mkfs fails": {
			req: deviceReq,
			sysCfg: &system.MockSysConfig{
				MkfsErr: errors.New("mock mkfs"),
			},
			expErr: errors.New("mock mkfs"),
		},
		"Mount fails": {
			req: deviceReq,
			mountCfg: &storage.MockMountProviderConfig{
				MountErr: errors.New("mock Mount"),
			},
			expErr: errors.New("mock Mount"),
		},
		"remove old data dir fails": {
			req: deviceReq,
			setup: func(t *testing.T, root string) func() {
				t.Helper()

				if err := os.Mkdir(root, 0400); err != nil {
					t.Fatal(err)
				}
				return func() {
					t.Helper()

					if err := os.Chmod(root, 0755); err != nil {
						t.Fatal(err)
					}
				}
			},
			expErr: errors.New("removing old control metadata subdirectory"),
		},
		"create data dir fails": {
			req: deviceReq,
			setup: func(t *testing.T, root string) func() {
				t.Helper()

				if err := os.Mkdir(root, 0555); err != nil {
					t.Fatal(err)
				}
				return func() {
					t.Helper()

					if err := os.Chmod(root, 0755); err != nil {
						t.Fatal(err)
					}
				}
			},
			expErr: errors.New("creating control metadata subdirectory"),
		},
		"chown data dir fails": {
			req: deviceReq,
			sysCfg: &system.MockSysConfig{
				ChownErr: errors.New("mock chown"),
			},
			expErr: errors.New("mock chown"),
		},
		"Unmount fails": {
			req: deviceReq,
			mountCfg: &storage.MockMountProviderConfig{
				IsMountedRes: true,
				UnmountErr:   errors.New("mock Unmount"),
			},
			expErr: errors.New("mock Unmount"),
		},
		"device success": {
			req: deviceReq,
		},
		"path only doesn't attempt device format": {
			req: pathReq,
			sysCfg: &system.MockSysConfig{
				MkfsErr:      errors.New("mkfs was called!"),
				GetFsTypeRes: &system.FsType{Name: system.FsTypeExt4},
			},
			mountCfg: &storage.MockMountProviderConfig{
				MountErr: errors.New("mount was called!"),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			testDir, cleanupTestDir := test.CreateTestDir(t)
			defer cleanupTestDir()

			// Point the paths at the testdir
			if tc.req.RootPath != "" {
				tc.req.RootPath = filepath.Join(testDir, tc.req.RootPath)
			}

			if tc.req.DataPath != "" {
				tc.req.DataPath = filepath.Join(testDir, tc.req.DataPath)
			}

			if tc.setup == nil {
				tc.setup = func(t *testing.T, root string) func() {
					if root != "" {
						if err := os.Mkdir(root, 0755); err != nil {
							t.Fatal(err)
						}
					}
					return func() {}
				}
			}
			teardown := tc.setup(t, tc.req.RootPath)
			defer teardown()

			var p *Provider
			if !tc.nilProv {
				p = NewProvider(log, system.NewMockSysProvider(log, tc.sysCfg), storage.NewMockMountProvider(tc.mountCfg))
			}

			err := p.Format(tc.req)

			test.CmpErr(t, tc.expErr, err)
		})
	}
}

func TestMetadata_Provider_NeedsFormat(t *testing.T) {
	deviceReq := storage.MetadataFormatRequest{
		RootPath: "/test_root",
		Device:   "/dev/something",
		DataPath: "/test_root/data",
		OwnerUID: 100,
		OwnerGID: 200,
	}
	pathReq := storage.MetadataFormatRequest{
		RootPath: "/test_root",
		DataPath: "/test_root/data",
		OwnerUID: 100,
		OwnerGID: 200,
	}

	for name, tc := range map[string]struct {
		nilProv   bool
		req       storage.MetadataFormatRequest
		setup     func(t *testing.T, root string) func()
		sysCfg    *system.MockSysConfig
		mountCfg  *storage.MockMountProviderConfig
		expResult bool
		expErr    error
	}{
		"nil": {
			nilProv: true,
			req:     pathReq,
			expErr:  errors.New("nil"),
		},
		"root stat failed": {
			req: pathReq,
			sysCfg: &system.MockSysConfig{
				GetfsStr: defaultDevFS,
				StatErrors: map[string]error{
					pathReq.RootPath: errors.New("mock Stat RootPath"),
				},
			},
			expErr: errors.New("mock Stat RootPath"),
		},
		"root path missing": {
			req: pathReq,
			sysCfg: &system.MockSysConfig{
				GetfsStr: defaultDevFS,
				StatErrors: map[string]error{
					pathReq.RootPath: os.ErrNotExist,
				},
			},
			expResult: true,
		},
		"path-only data stat failed": {
			req: pathReq,
			sysCfg: &system.MockSysConfig{
				GetfsStr: defaultDevFS,
				StatErrors: map[string]error{
					pathReq.DataPath: errors.New("mock Stat DataPath"),
				},
			},
			expErr: errors.New("mock Stat DataPath"),
		},
		"path-only data path missing": {
			req: pathReq,
			sysCfg: &system.MockSysConfig{
				GetfsStr: defaultDevFS,
				StatErrors: map[string]error{
					pathReq.DataPath: os.ErrNotExist,
				},
			},
			expResult: true,
		},
		"path-only nothing needed": {
			req: pathReq,
			mountCfg: &storage.MockMountProviderConfig{
				MountErr: errors.New("mount was called!"),
			},
		},
		"device getfs failed": {
			req: deviceReq,
			sysCfg: &system.MockSysConfig{
				GetfsErr: errors.New("mock Getfs"),
			},
			expErr: errors.New("mock Getfs"),
		},
		"device with wrong fs": {
			req: deviceReq,
			sysCfg: &system.MockSysConfig{
				GetfsStr:   "bad",
				StatErrors: map[string]error{},
			},
			expResult: true,
		},
		"device mount failed": {
			req: deviceReq,
			mountCfg: &storage.MockMountProviderConfig{
				MountErr: errors.New("mock Mount"),
			},
			expErr: errors.New("mock Mount"),
		},
		"device already mounted": {
			req: deviceReq,
			mountCfg: &storage.MockMountProviderConfig{
				MountErr: errors.Wrap(storage.FaultTargetAlreadyMounted, "mock Mount"),
			},
		},
		"device data stat failed": {
			req: deviceReq,
			sysCfg: &system.MockSysConfig{
				GetfsStr: defaultDevFS,
				StatErrors: map[string]error{
					pathReq.DataPath: errors.New("mock Stat DataPath"),
				},
			},
			expErr: errors.New("mock Stat DataPath"),
		},
		"device data path missing": {
			req: deviceReq,
			sysCfg: &system.MockSysConfig{
				GetfsStr: defaultDevFS,
				StatErrors: map[string]error{
					pathReq.DataPath: os.ErrNotExist,
				},
			},
			expResult: true,
		},
		"device data nothing needed": {
			req: deviceReq,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			testDir, cleanupTestDir := test.CreateTestDir(t)
			defer cleanupTestDir()

			if tc.sysCfg == nil {
				tc.sysCfg = &system.MockSysConfig{
					StatErrors: map[string]error{},
					GetfsStr:   defaultDevFS,
				}
			}

			fixSysCfg := func(oldPath, newPath string) {
				if tc.sysCfg.StatErrors != nil {
					if statErr, exists := tc.sysCfg.StatErrors[oldPath]; exists {
						tc.sysCfg.StatErrors[newPath] = statErr
					}
				}
			}

			// Point the paths at the testdir
			if tc.req.RootPath != "" {
				oldRootPath := tc.req.RootPath
				tc.req.RootPath = filepath.Join(testDir, tc.req.RootPath)
				fixSysCfg(oldRootPath, tc.req.RootPath)
			}

			if tc.req.DataPath != "" {
				oldDataPath := tc.req.DataPath
				tc.req.DataPath = filepath.Join(testDir, tc.req.DataPath)
				fixSysCfg(oldDataPath, tc.req.DataPath)
			}

			if tc.setup == nil {
				tc.setup = func(t *testing.T, root string) func() {
					if root != "" {
						if err := os.Mkdir(root, 0755); err != nil {
							t.Fatal(err)
						}
					}
					return func() {}
				}
			}
			teardown := tc.setup(t, tc.req.RootPath)
			defer teardown()

			var p *Provider
			if !tc.nilProv {
				p = NewProvider(log, system.NewMockSysProvider(log, tc.sysCfg), storage.NewMockMountProvider(tc.mountCfg))
			}

			result, err := p.NeedsFormat(tc.req)

			test.CmpErr(t, tc.expErr, err)
			test.AssertEqual(t, tc.expResult, result, "")
		})
	}
}

func TestMetadata_Provider_Mount(t *testing.T) {
	for name, tc := range map[string]struct {
		nilProv  bool
		mountCfg *storage.MockMountProviderConfig
		req      storage.MetadataMountRequest
		expResp  *storage.MountResponse
		expErr   error
	}{
		"nil provider": {
			nilProv: true,
			req: storage.MetadataMountRequest{
				RootPath: "/something",
				Device:   "/dev/something",
			},
			expErr: errors.New("nil"),
		},
		"no root path": {
			req: storage.MetadataMountRequest{
				Device: "/dev/something",
			},
			expErr: errors.New("no control metadata root path"),
		},
		"path only case does not call mount": {
			req: storage.MetadataMountRequest{
				RootPath: "/something",
			},
			mountCfg: &storage.MockMountProviderConfig{
				// Mount shouldn't be called in this case
				MountErr: errors.New("mount was called!"),
			},
			expResp: &storage.MountResponse{
				Target: "/something",
			},
		},
		"mount device fails": {
			req: storage.MetadataMountRequest{
				RootPath: "/something",
				Device:   "/dev/something",
			},
			mountCfg: &storage.MockMountProviderConfig{
				MountErr: errors.New("mock mount"),
			},
			expErr: errors.New("mock mount"),
		},
		"success": {
			req: storage.MetadataMountRequest{
				RootPath: "/something",
				Device:   "/dev/something",
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
				p = NewProvider(log, nil, storage.NewMockMountProvider(tc.mountCfg))
			}

			resp, err := p.Mount(tc.req)

			test.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.expResp, resp); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestMetadata_Provider_Unmount(t *testing.T) {
	for name, tc := range map[string]struct {
		nilProv  bool
		mountCfg *storage.MockMountProviderConfig
		req      storage.MetadataMountRequest
		expResp  *storage.MountResponse
		expErr   error
	}{
		"nil provider": {
			nilProv: true,
			req: storage.MetadataMountRequest{
				RootPath: "/something",
			},
			expErr: errors.New("nil"),
		},
		"no root path": {
			req: storage.MetadataMountRequest{
				Device: "/dev/something",
			},
			expErr: errors.New("no control metadata root path"),
		},
		"isMounted fails": {
			req: storage.MetadataMountRequest{
				RootPath: "/something",
			},
			mountCfg: &storage.MockMountProviderConfig{
				IsMountedErr: errors.New("mock IsMounted"),
			},
			expErr: errors.New("mock IsMounted"),
		},
		"not mounted": {
			req: storage.MetadataMountRequest{
				RootPath: "/something",
			},
			mountCfg: &storage.MockMountProviderConfig{
				IsMountedRes: false,
			},
			expResp: &storage.MountResponse{
				Target:  "/something",
				Mounted: false,
			},
		},
		"unmount fails": {
			req: storage.MetadataMountRequest{
				RootPath: "/something",
			},
			mountCfg: &storage.MockMountProviderConfig{
				IsMountedRes: true,
				UnmountErr:   errors.New("mock Unmount"),
			},
			expErr: errors.New("mock Unmount"),
		},
		"unmount success": {
			req: storage.MetadataMountRequest{
				RootPath: "/something",
			},
			mountCfg: &storage.MockMountProviderConfig{
				IsMountedRes: true,
			},
			expResp: &storage.MountResponse{
				Target:  "/something",
				Mounted: false,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			var p *Provider
			if !tc.nilProv {
				p = NewProvider(log, nil, storage.NewMockMountProvider(tc.mountCfg))
			}

			resp, err := p.Unmount(tc.req)

			test.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.expResp, resp); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}
