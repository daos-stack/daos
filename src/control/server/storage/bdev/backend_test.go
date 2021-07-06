//
// (C) Copyright 2018-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package bdev

import (
	"encoding/json"
	"os"
	"path/filepath"
	"syscall"
	"testing"
	"time"

	"github.com/dustin/go-humanize"
	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/spdk"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// defCmpOpts returns a default set of cmp option suitable for this package
func defCmpOpts() []cmp.Option {
	return []cmp.Option{
		// ignore these fields on most tests, as they are intentionally not stable
		cmpopts.IgnoreFields(storage.NvmeController{}, "HealthStats", "Serial"),
	}
}

func convertTypes(in interface{}, out interface{}) error {
	data, err := json.Marshal(in)
	if err != nil {
		return err
	}

	return json.Unmarshal(data, out)
}

func mockSpdkController(varIdx ...int32) storage.NvmeController {
	native := storage.MockNvmeController(varIdx...)

	s := new(storage.NvmeController)
	if err := convertTypes(native, s); err != nil {
		panic(err)
	}

	return *s
}

func backendWithMockBinding(log logging.Logger, mec spdk.MockEnvCfg, mnc spdk.MockNvmeCfg) *spdkBackend {
	return &spdkBackend{
		log: log,
		binding: &spdkWrapper{
			Env:  &spdk.MockEnvImpl{Cfg: mec},
			Nvme: &spdk.MockNvmeImpl{Cfg: mnc},
		},
	}
}

func TestBackend_Scan(t *testing.T) {
	ctrlr1 := storage.MockNvmeController(1)

	for name, tc := range map[string]struct {
		req     ScanRequest
		mec     spdk.MockEnvCfg
		mnc     spdk.MockNvmeCfg
		expResp *ScanResponse
		expErr  error
	}{
		"binding scan fail": {
			mnc: spdk.MockNvmeCfg{
				DiscoverErr: errors.New("spdk says no"),
			},
			expErr: errors.New("spdk says no"),
		},
		"empty results from binding": {
			req:     ScanRequest{},
			expResp: &ScanResponse{},
		},
		"binding scan success": {
			mnc: spdk.MockNvmeCfg{
				DiscoverCtrlrs: storage.NvmeControllers{ctrlr1},
			},
			req: ScanRequest{},
			expResp: &ScanResponse{
				Controllers: storage.NvmeControllers{ctrlr1},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			b := backendWithMockBinding(log, tc.mec, tc.mnc)

			gotResp, gotErr := b.Scan(tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, defCmpOpts()...); diff != "" {
				t.Fatalf("\nunexpected output (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestBackend_Format(t *testing.T) {
	pci1 := storage.MockNvmeController(1).PciAddr
	pci2 := storage.MockNvmeController(2).PciAddr
	pci3 := storage.MockNvmeController(3).PciAddr

	testDir, clean := common.CreateTestDir(t)
	defer clean()

	for name, tc := range map[string]struct {
		req     FormatRequest
		mec     spdk.MockEnvCfg
		mnc     spdk.MockNvmeCfg
		expResp *FormatResponse
		expErr  error
	}{
		"unknown device class": {
			req: FormatRequest{
				Class:      storage.BdevClass("whoops"),
				DeviceList: []string{pci1},
			},
			expErr: FaultFormatUnknownClass("whoops"),
		},
		"aio file device class": {
			mec: spdk.MockEnvCfg{
				InitErr: errors.New("spdk backend init should not be called for non-nvme class"),
			},
			mnc: spdk.MockNvmeCfg{
				FormatErr: errors.New("spdk backend format should not be called for non-nvme class"),
			},
			req: FormatRequest{
				Class:          storage.BdevClassFile,
				DeviceList:     []string{filepath.Join(testDir, "daos-bdev")},
				DeviceFileSize: humanize.MiByte,
			},
			expResp: &FormatResponse{
				DeviceResponses: map[string]*DeviceFormatResponse{
					filepath.Join(testDir, "daos-bdev"): new(DeviceFormatResponse),
				},
			},
		},
		"aio kdev device class": {
			mec: spdk.MockEnvCfg{
				InitErr: errors.New("spdk backend init should not be called for non-nvme class"),
			},
			mnc: spdk.MockNvmeCfg{
				FormatErr: errors.New("spdk backend format should not be called for non-nvme class"),
			},
			req: FormatRequest{
				Class:      storage.BdevClassKdev,
				DeviceList: []string{"/dev/sdc", "/dev/sdd"},
			},
			expResp: &FormatResponse{
				DeviceResponses: map[string]*DeviceFormatResponse{
					"/dev/sdc": new(DeviceFormatResponse),
					"/dev/sdd": new(DeviceFormatResponse),
				},
			},
		},
		"binding format fail": {
			mnc: spdk.MockNvmeCfg{
				FormatErr: errors.New("spdk says no"),
			},
			req: FormatRequest{
				Class:      storage.BdevClassNvme,
				DeviceList: []string{pci1},
			},
			expErr: errors.New("spdk says no"),
		},
		"empty results from binding": {
			req: FormatRequest{
				Class:      storage.BdevClassNvme,
				DeviceList: []string{pci1},
			},
			expErr: errors.New("empty results from spdk binding format request"),
		},
		"binding format success": {
			mnc: spdk.MockNvmeCfg{
				FormatRes: []*spdk.FormatResult{
					{CtrlrPCIAddr: pci1, NsID: 1},
				},
			},
			req: FormatRequest{
				Class:      storage.BdevClassNvme,
				DeviceList: []string{pci1},
			},
			expResp: &FormatResponse{
				DeviceResponses: map[string]*DeviceFormatResponse{
					pci1: {
						Formatted: true,
					},
				},
			},
		},
		"multiple ssd and namespace success": {
			mnc: spdk.MockNvmeCfg{
				FormatRes: []*spdk.FormatResult{
					{CtrlrPCIAddr: pci1, NsID: 1},
					{CtrlrPCIAddr: pci1, NsID: 2},
					{CtrlrPCIAddr: pci2, NsID: 2},
					{CtrlrPCIAddr: pci2, NsID: 1},
					{CtrlrPCIAddr: pci3, NsID: 1},
					{CtrlrPCIAddr: pci3, NsID: 2},
				},
			},
			req: FormatRequest{
				Class:      storage.BdevClassNvme,
				DeviceList: []string{pci1, pci2, pci3},
			},
			expResp: &FormatResponse{
				DeviceResponses: DeviceFormatResponses{
					pci1: &DeviceFormatResponse{
						Formatted: true,
					},
					pci2: &DeviceFormatResponse{
						Formatted: true,
					},
					pci3: &DeviceFormatResponse{
						Formatted: true,
					},
				},
			},
		},
		"two success and one failure": {
			mnc: spdk.MockNvmeCfg{
				FormatRes: []*spdk.FormatResult{
					{CtrlrPCIAddr: pci1, NsID: 1},
					{CtrlrPCIAddr: pci1, NsID: 2},
					{CtrlrPCIAddr: pci2, NsID: 2},
					{CtrlrPCIAddr: pci2, NsID: 1},
					{CtrlrPCIAddr: pci3, NsID: 1},
					{
						CtrlrPCIAddr: pci3, NsID: 2,
						Err: errors.New("spdk format failed"),
					},
				},
			},
			req: FormatRequest{
				Class:      storage.BdevClassNvme,
				DeviceList: []string{pci1, pci2, pci3},
			},
			expResp: &FormatResponse{
				DeviceResponses: DeviceFormatResponses{
					pci1: &DeviceFormatResponse{
						Formatted: true,
					},
					pci2: &DeviceFormatResponse{
						Formatted: true,
					},
					pci3: &DeviceFormatResponse{
						Error: FaultFormatError(
							pci3,
							errors.Errorf(
								"failed to format namespaces [2] (namespace 2: %s)",
								errors.New("spdk format failed"))),
					},
				},
			},
		},
		"multiple namespaces on single controller success": {
			mnc: spdk.MockNvmeCfg{
				FormatRes: []*spdk.FormatResult{
					{CtrlrPCIAddr: pci1, NsID: 1},
					{CtrlrPCIAddr: pci1, NsID: 2},
					{CtrlrPCIAddr: pci1, NsID: 3},
					{CtrlrPCIAddr: pci1, NsID: 4},
				},
			},
			req: FormatRequest{
				Class:      storage.BdevClassNvme,
				DeviceList: []string{pci1},
			},
			expResp: &FormatResponse{
				DeviceResponses: DeviceFormatResponses{
					pci1: &DeviceFormatResponse{
						Formatted: true,
					},
				},
			},
		},
		"multiple namespaces on single controller failure": {
			mnc: spdk.MockNvmeCfg{
				FormatRes: []*spdk.FormatResult{
					{
						CtrlrPCIAddr: pci1, NsID: 2,
						Err: errors.New("spdk format failed"),
					},
					{
						CtrlrPCIAddr: pci1, NsID: 3,
						Err: errors.New("spdk format failed"),
					},
					{
						CtrlrPCIAddr: pci1, NsID: 4,
						Err: errors.New("spdk format failed"),
					},
					{
						CtrlrPCIAddr: pci1, NsID: 1,
						Err: errors.New("spdk format failed"),
					},
				},
			},
			req: FormatRequest{
				Class:      storage.BdevClassNvme,
				DeviceList: []string{pci1},
			},
			expResp: &FormatResponse{
				DeviceResponses: DeviceFormatResponses{
					pci1: &DeviceFormatResponse{
						Error: FaultFormatError(
							pci1,
							errors.Errorf(
								"failed to format namespaces [1 2 3 4] (namespace 1: %s)",
								errors.New("spdk format failed"))),
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			b := backendWithMockBinding(log, tc.mec, tc.mnc)
			b.script = mockScriptRunner(log)

			// output path would be set during config validate
			tc.req.ConfigPath = filepath.Join(testDir, storage.BdevOutConfName)
			tc.req.OwnerUID = os.Geteuid()
			tc.req.OwnerGID = os.Getegid()

			gotResp, gotErr := b.Format(tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, defCmpOpts()...); diff != "" {
				t.Fatalf("\nunexpected output (-want, +got):\n%s\n", diff)
			}

			if tc.req.Class != storage.BdevClassFile {
				return
			}

			// verify empty files created for AIO class
			for _, testFile := range tc.req.DeviceList {
				if _, err := os.Stat(testFile); err != nil {
					t.Fatal(err)
				}
			}
		})
	}
}

func TestBackend_Update(t *testing.T) {
	numCtrlrs := 4
	controllers := make(storage.NvmeControllers, 0, numCtrlrs)
	for i := 0; i < numCtrlrs; i++ {
		c := mockSpdkController(int32(i))
		controllers = append(controllers, &c)
	}

	for name, tc := range map[string]struct {
		pciAddr string
		mec     spdk.MockEnvCfg
		mnc     spdk.MockNvmeCfg
		expErr  error
	}{
		"no PCI addr": {
			expErr: FaultBadPCIAddr(""),
		},
		"binding update fail": {
			pciAddr: controllers[0].PciAddr,
			mnc: spdk.MockNvmeCfg{
				UpdateErr: errors.New("spdk says no"),
			},
			expErr: errors.New("spdk says no"),
		},
		"binding update success": {
			pciAddr: controllers[0].PciAddr,
			expErr:  nil,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			b := backendWithMockBinding(log, tc.mec, tc.mnc)

			gotErr := b.UpdateFirmware(tc.pciAddr, "/some/path", 0)
			common.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

type mockFileInfo struct {
	name    string
	size    int64
	mode    os.FileMode
	modTime time.Time
	isDir   bool
	stat    *syscall.Stat_t
}

func (mfi *mockFileInfo) Name() string       { return mfi.name }
func (mfi *mockFileInfo) Size() int64        { return mfi.size }
func (mfi *mockFileInfo) Mode() os.FileMode  { return mfi.mode }
func (mfi *mockFileInfo) ModTime() time.Time { return mfi.modTime }
func (mfi *mockFileInfo) IsDir() bool        { return mfi.isDir }
func (mfi *mockFileInfo) Sys() interface{}   { return mfi.stat }

func testFileInfo(t *testing.T, name string, uid uint32) os.FileInfo {
	t.Helper()

	return &mockFileInfo{
		name: name,
		stat: &syscall.Stat_t{
			Uid: uid,
		},
	}
}

type testWalkInput struct {
	path   string
	info   os.FileInfo
	err    error
	expErr error
}

func TestBackend_cleanHugePagesFn(t *testing.T) {
	testDir := "/wherever"

	for name, tc := range map[string]struct {
		prefix     string
		tgtUID     string
		testInputs []*testWalkInput
		removeErr  error
		expRemoved []string
	}{
		"ignore subdirectory": {
			prefix: "prefix1",
			tgtUID: "42",
			testInputs: []*testWalkInput{
				{
					path: filepath.Join(testDir, "prefix1_foo"),
					info: &mockFileInfo{
						name: "prefix1_foo",
						stat: &syscall.Stat_t{
							Uid: 42,
						},
						isDir: true,
					},
					expErr: errors.New("skip this directory"),
				},
			},
			expRemoved: []string{},
		},
		"input error propagated": {
			testInputs: []*testWalkInput{
				{
					path:   filepath.Join(testDir, "prefix1_foo"),
					info:   testFileInfo(t, "prefix1_foo", 42),
					err:    errors.New("walk failed"),
					expErr: errors.New("walk failed"),
				},
			},
			expRemoved: []string{},
		},
		"nil fileinfo": {
			testInputs: []*testWalkInput{
				{
					path:   filepath.Join(testDir, "prefix1_foo"),
					info:   nil,
					expErr: errors.New("nil fileinfo"),
				},
			},
			expRemoved: []string{},
		},
		"nil file stat": {
			prefix: "prefix1",
			tgtUID: "42",
			testInputs: []*testWalkInput{
				{
					path: filepath.Join(testDir, "prefix1_foo"),
					info: &mockFileInfo{
						name: "prefix1_foo",
						stat: nil,
					},
					expErr: errors.New("stat missing for file"),
				},
			},
			expRemoved: []string{},
		},
		"prefix matching": {
			prefix: "prefix1",
			tgtUID: "42",
			testInputs: []*testWalkInput{
				{
					path: filepath.Join(testDir, "prefix2_foo"),
					info: testFileInfo(t, "prefix2_foo", 42),
				},
				{
					path: filepath.Join(testDir, "prefix1_foo"),
					info: testFileInfo(t, "prefix1_foo", 42),
				},
			},
			expRemoved: []string{filepath.Join(testDir, "prefix1_foo")},
		},
		"uid matching": {
			prefix: "prefix1",
			tgtUID: "42",
			testInputs: []*testWalkInput{
				{
					path: filepath.Join(testDir, "prefix1_foo"),
					info: testFileInfo(t, "prefix1_foo", 41),
				},
				{
					path: filepath.Join(testDir, "prefix1_bar"),
					info: testFileInfo(t, "prefix1_bar", 42),
				},
			},
			expRemoved: []string{filepath.Join(testDir, "prefix1_bar")},
		},
		"remove fails": {
			prefix: "prefix1",
			tgtUID: "42",
			testInputs: []*testWalkInput{
				{
					path:   filepath.Join(testDir, "prefix1_foo"),
					info:   testFileInfo(t, "prefix1_foo", 42),
					expErr: errors.New("could not remove"),
				},
			},
			expRemoved: []string{},
			removeErr:  errors.New("could not remove"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			removedFiles := make([]string, 0)
			removeFn := func(path string) error {
				if tc.removeErr == nil {
					removedFiles = append(removedFiles, path)
				}
				return tc.removeErr
			}

			testFn := hugePageWalkFunc(testDir, tc.prefix, tc.tgtUID, removeFn)
			for _, ti := range tc.testInputs {
				gotErr := testFn(ti.path, ti.info, ti.err)
				common.CmpErr(t, ti.expErr, gotErr)
			}
			if diff := cmp.Diff(tc.expRemoved, removedFiles); diff != "" {
				t.Fatalf("unexpected remove result (-want, +got):\n%s\n", diff)
			}
		})
	}
}
