//
// (C) Copyright 2022-2024 Intel Corporation.
// (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
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
		nilProv     bool
		sysCfg      *system.MockSysConfig
		mountCfg    *storage.MockMountProviderConfig
		setup       func(*testing.T, string) func()
		req         storage.MetadataFormatRequest
		expErr      error
		expMkfs     bool
		expMkfsOpts []string
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
		"GetfsType fails": {
			req: pathReq,
			sysCfg: &system.MockSysConfig{
				GetfsTypeErr: []error{errors.New("mock GetfsType")},
			},
			expErr: errors.New("mock GetfsType"),
		},
		"GetfsType returns nosuid flag": {
			req: pathReq,
			sysCfg: &system.MockSysConfig{
				GetfsTypeRes: &system.FsType{
					Name:   system.FsTypeExt4,
					NoSUID: true,
				},
			},
			expErr: FaultBadFilesystem(&system.FsType{
				Name:   system.FsTypeExt4,
				NoSUID: true,
			}),
		},
		"GetfsType returns nfs": {
			req: pathReq,
			sysCfg: &system.MockSysConfig{
				GetfsTypeRes: &system.FsType{Name: system.FsTypeNfs},
			},
			expErr: FaultBadFilesystem(&system.FsType{Name: system.FsTypeNfs}),
		},
		"GetfsType returns unknown": {
			req: pathReq,
			sysCfg: &system.MockSysConfig{
				GetfsTypeRes: &system.FsType{Name: system.FsTypeUnknown},
			},
		},
		"GetfsType skipped with device": {
			req: deviceReq,
			sysCfg: &system.MockSysConfig{
				GetfsTypeErr: []error{errors.New("mock GetfsType")},
			},
			expMkfsOpts: []string{"-q"},
			expMkfs:     true,
		},
		"GetfsType retries with parent if dir doesn't exist": {
			req: pathReq,
			sysCfg: &system.MockSysConfig{
				GetfsTypeRes: &system.FsType{Name: system.FsTypeExt4},
				GetfsTypeErr: []error{os.ErrNotExist, os.ErrNotExist, nil},
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
		"get label fails": {
			req: deviceReq,
			sysCfg: &system.MockSysConfig{
				GetDeviceLabelErr: errors.New("mock GetDeviceLabel"),
			},
			expErr: errors.New("mock GetDeviceLabel"),
		},
		"mkfs fails": {
			req: deviceReq,
			sysCfg: &system.MockSysConfig{
				MkfsErr: errors.New("mock mkfs"),
			},
			expErr:      errors.New("mock mkfs"),
			expMkfsOpts: []string{"-q"},
			expMkfs:     true,
		},
		"Mount fails": {
			req: deviceReq,
			mountCfg: &storage.MockMountProviderConfig{
				MountErr: errors.New("mock Mount"),
			},
			expErr:      errors.New("mock Mount"),
			expMkfsOpts: []string{"-q"},
			expMkfs:     true,
		},
		"remove old data dir fails with permission denied": {
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
			expErr:      errors.New("permission denied"),
			expMkfsOpts: []string{"-q"},
			expMkfs:     true,
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
			expErr:      errors.New("creating control metadata subdirectory"),
			expMkfsOpts: []string{"-q"},
			expMkfs:     true,
		},
		"chown data dir fails": {
			req: deviceReq,
			sysCfg: &system.MockSysConfig{
				ChownErr: errors.New("mock chown"),
			},
			expErr:      errors.New("mock chown"),
			expMkfsOpts: []string{"-q"},
			expMkfs:     true,
		},
		"Unmount fails": {
			req: deviceReq,
			mountCfg: &storage.MockMountProviderConfig{
				IsMountedRes: true,
				UnmountErr:   errors.New("mock Unmount"),
			},
			expErr:      errors.New("mock Unmount"),
			expMkfsOpts: []string{"-q"},
			expMkfs:     true,
		},
		"device success": {
			req:         deviceReq,
			expMkfsOpts: []string{"-q"},
			expMkfs:     true,
		},
		"preserve existing label": {
			req: deviceReq,
			sysCfg: &system.MockSysConfig{
				GetDeviceLabelRes: "old_label",
			},
			expMkfsOpts: []string{"-q", "-L", "old_label"},
			expMkfs:     true,
		},
		"path only; doesn't attempt device format": {
			req: pathReq,
			sysCfg: &system.MockSysConfig{
				MkfsErr:      errors.New("mkfs was called!"),
				GetfsTypeRes: &system.FsType{Name: system.FsTypeExt4},
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

			if tc.sysCfg == nil {
				tc.sysCfg = new(system.MockSysConfig)
			}
			tc.sysCfg.RealMkdir = true
			tc.sysCfg.RealRemoveAll = true

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
			mockSys := system.NewMockSysProvider(log, tc.sysCfg)
			if !tc.nilProv {
				p = NewProvider(log, mockSys, storage.NewMockMountProvider(tc.mountCfg))
			}

			err := p.Format(tc.req)

			test.CmpErr(t, tc.expErr, err)

			if tc.expMkfs {
				test.AssertEqual(t, 1, len(mockSys.MkfsReqs), "should have called mkfs")
				if diff := cmp.Diff(tc.expMkfsOpts, mockSys.MkfsReqs[0].Options); diff != "" {
					t.Errorf("unexpected mkfs options (-want +got):\n%s\n", diff)
				}
			} else {
				test.AssertEqual(t, 0, len(mockSys.MkfsReqs), "should not have called mkfs")
			}
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

// TestMetadata_Provider_setupDataDir_SelectiveEngineDelete tests the new selective
// engine directory deletion feature for DAOS-19385.
func TestMetadata_Provider_setupDataDir_SelectiveEngineDelete(t *testing.T) {
	for name, tc := range map[string]struct {
		setupExisting func(t *testing.T, dataPath string)
		req           storage.MetadataFormatRequest
		sysCfg        *system.MockSysConfig
		expErr        error
		expExist      []uint
		expNotExist   []uint
	}{
		"selective delete: engines 0 and 2 when datapath exists": {
			setupExisting: func(t *testing.T, dataPath string) {
				// Create the data directory and subdirectories for engines 0, 1, 2
				if err := os.MkdirAll(dataPath, 0775); err != nil {
					t.Fatal(err)
				}
				for _, idx := range []uint{0, 1, 2} {
					engPath := storage.ControlMetadataEngineDir(dataPath, idx)
					if err := os.MkdirAll(engPath, 0775); err != nil {
						t.Fatal(err)
					}
					testFile := filepath.Join(engPath, "test.dat")
					if err := os.WriteFile(testFile, []byte("test"), 0644); err != nil {
						t.Fatal(err)
					}
				}
			},
			req: storage.MetadataFormatRequest{
				RootPath:   "/test_root",
				DataPath:   "/test_root/data",
				OwnerUID:   100,
				OwnerGID:   200,
				EngineIdxs: []uint{0, 2},
			},
			expExist:    []uint{1},
			expNotExist: []uint{},
		},
		"selective delete: single engine when others exist": {
			setupExisting: func(t *testing.T, dataPath string) {
				if err := os.MkdirAll(dataPath, 0775); err != nil {
					t.Fatal(err)
				}
				for _, idx := range []uint{0, 1, 2, 3} {
					engPath := storage.ControlMetadataEngineDir(dataPath, idx)
					if err := os.MkdirAll(engPath, 0775); err != nil {
						t.Fatal(err)
					}
				}
			},
			req: storage.MetadataFormatRequest{
				RootPath:   "/test_root",
				DataPath:   "/test_root/data",
				OwnerUID:   100,
				OwnerGID:   200,
				EngineIdxs: []uint{1},
			},
			expExist:    []uint{0, 2, 3},
			expNotExist: []uint{},
		},
		"datapath doesn't exist: create with specific engines": {
			setupExisting: func(t *testing.T, dataPath string) {
				// Don't create anything
			},
			req: storage.MetadataFormatRequest{
				RootPath:   "/test_root",
				DataPath:   "/test_root/data",
				OwnerUID:   100,
				OwnerGID:   200,
				EngineIdxs: []uint{0, 2},
			},
			expExist:    []uint{},
			expNotExist: []uint{1, 3},
		},
		"empty engine list: remove all (legacy behavior)": {
			setupExisting: func(t *testing.T, dataPath string) {
				if err := os.MkdirAll(dataPath, 0775); err != nil {
					t.Fatal(err)
				}
				for _, idx := range []uint{0, 1, 2} {
					engPath := storage.ControlMetadataEngineDir(dataPath, idx)
					if err := os.MkdirAll(engPath, 0775); err != nil {
						t.Fatal(err)
					}
				}
			},
			req: storage.MetadataFormatRequest{
				RootPath:   "/test_root",
				DataPath:   "/test_root/data",
				OwnerUID:   100,
				OwnerGID:   200,
				EngineIdxs: []uint{},
			},
			expExist:    []uint{},
			expNotExist: []uint{0, 1, 2},
		},
		"stat error on datapath": {
			setupExisting: func(t *testing.T, dataPath string) {
				// Setup doesn't matter
			},
			req: storage.MetadataFormatRequest{
				RootPath:   "/test_root",
				DataPath:   "/test_root/data",
				OwnerUID:   100,
				OwnerGID:   200,
				EngineIdxs: []uint{0, 1},
			},
			sysCfg: &system.MockSysConfig{
				StatErrors: map[string]error{
					"/test_root/data": errors.New("mock stat error"),
				},
			},
			expErr: errors.New("mock stat error"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			testDir, cleanupTestDir := test.CreateTestDir(t)
			defer cleanupTestDir()

			oldDataPath := tc.req.DataPath
			tc.req.RootPath = filepath.Join(testDir, tc.req.RootPath)
			tc.req.DataPath = filepath.Join(testDir, tc.req.DataPath)

			if tc.sysCfg == nil {
				tc.sysCfg = &system.MockSysConfig{}
			}
			// Enable real file operations for testing (unless StatErrors configured)
			if tc.sysCfg.StatErrors == nil {
				tc.sysCfg.RealStat = true
			}
			tc.sysCfg.RealMkdir = true
			tc.sysCfg.RealRemoveAll = true

			if tc.sysCfg.StatErrors != nil {
				if statErr, exists := tc.sysCfg.StatErrors[oldDataPath]; exists {
					tc.sysCfg.StatErrors[tc.req.DataPath] = statErr
				}
			}

			if tc.setupExisting != nil {
				tc.setupExisting(t, tc.req.DataPath)
			}

			// Ensure parent directory exists for tests where DataPath doesn't exist
			if tc.req.RootPath != "" {
				if err := os.MkdirAll(tc.req.RootPath, 0775); err != nil && !os.IsExist(err) {
					t.Fatal(err)
				}
			}

			p := NewProvider(log, system.NewMockSysProvider(log, tc.sysCfg), nil)

			err := p.setupDataDir(tc.req)

			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			for _, idx := range tc.expExist {
				engPath := storage.ControlMetadataEngineDir(tc.req.DataPath, idx)
				if _, err := os.Stat(engPath); os.IsNotExist(err) {
					t.Errorf("expected engine %d directory to exist at %s", idx, engPath)
				}
			}

			for _, idx := range tc.expNotExist {
				engPath := storage.ControlMetadataEngineDir(tc.req.DataPath, idx)
				if _, err := os.Stat(engPath); err == nil {
					t.Errorf("expected engine %d directory NOT to exist at %s", idx, engPath)
				}
			}

			for _, idx := range tc.req.EngineIdxs {
				engPath := storage.ControlMetadataEngineDir(tc.req.DataPath, idx)
				if _, err := os.Stat(engPath); os.IsNotExist(err) {
					t.Errorf("expected engine %d directory created at %s", idx, engPath)
				}
			}
		})
	}
}
