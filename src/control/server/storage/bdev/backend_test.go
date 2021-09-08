//
// (C) Copyright 2018-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package bdev

import (
	"encoding/json"
	"fmt"
	"os"
	"os/user"
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

const (
	vmdAddr         = "0000:5d:05.5"
	vmdBackingAddr1 = "5d0505:01:00.0"
	vmdBackingAddr2 = "5d0505:03:00.0"
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

func mockCtrlrsInclVMD() storage.NvmeControllers {
	bdevAddrs := []string{
		"0000:90:00.0", "0000:d8:00.0", vmdBackingAddr1, "0000:8e:00.0",
		"0000:8a:00.0", "0000:8d:00.0", "0000:8b:00.0", "0000:8c:00.0",
		"0000:8f:00.0", vmdBackingAddr2,
	}
	bdevCtrlrs := make(storage.NvmeControllers, len(bdevAddrs))
	for idx, addr := range bdevAddrs {
		bdevCtrlrs[idx] = &storage.NvmeController{PciAddr: addr}
	}
	return bdevCtrlrs
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
		req     storage.BdevScanRequest
		mec     spdk.MockEnvCfg
		mnc     spdk.MockNvmeCfg
		expResp *storage.BdevScanResponse
		expErr  error
	}{
		"binding scan fail": {
			mnc: spdk.MockNvmeCfg{
				DiscoverErr: errors.New("spdk says no"),
			},
			expErr: errors.New("spdk says no"),
		},
		"empty results from binding": {
			req:     storage.BdevScanRequest{},
			expResp: &storage.BdevScanResponse{},
		},
		"binding scan success": {
			mnc: spdk.MockNvmeCfg{
				DiscoverCtrlrs: storage.NvmeControllers{ctrlr1},
			},
			req: storage.BdevScanRequest{},
			expResp: &storage.BdevScanResponse{
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
		req         storage.BdevFormatRequest
		mec         spdk.MockEnvCfg
		mnc         spdk.MockNvmeCfg
		expResp     *storage.BdevFormatResponse
		expErr      error
		expInitOpts []*spdk.EnvOptions
	}{
		"unknown device class": {
			req: storage.BdevFormatRequest{
				Properties: storage.BdevTierProperties{
					Class:      storage.Class("whoops"),
					DeviceList: []string{pci1},
				},
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
			req: storage.BdevFormatRequest{
				Properties: storage.BdevTierProperties{
					Class:          storage.ClassFile,
					DeviceList:     []string{filepath.Join(testDir, "daos-bdev")},
					DeviceFileSize: humanize.MiByte,
				},
			},
			expResp: &storage.BdevFormatResponse{
				DeviceResponses: map[string]*storage.BdevDeviceFormatResponse{
					filepath.Join(testDir, "daos-bdev"): new(storage.BdevDeviceFormatResponse),
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
			req: storage.BdevFormatRequest{
				Properties: storage.BdevTierProperties{
					Class:      storage.ClassKdev,
					DeviceList: []string{"/dev/sdc", "/dev/sdd"},
				},
			},
			expResp: &storage.BdevFormatResponse{
				DeviceResponses: map[string]*storage.BdevDeviceFormatResponse{
					"/dev/sdc": new(storage.BdevDeviceFormatResponse),
					"/dev/sdd": new(storage.BdevDeviceFormatResponse),
				},
			},
		},
		"binding format fail": {
			mnc: spdk.MockNvmeCfg{
				FormatErr: errors.New("spdk says no"),
			},
			req: storage.BdevFormatRequest{
				Properties: storage.BdevTierProperties{
					Class:      storage.ClassNvme,
					DeviceList: []string{pci1},
				},
			},
			expErr: errors.New("spdk says no"),
		},
		"empty results from binding": {
			req: storage.BdevFormatRequest{
				Properties: storage.BdevTierProperties{
					Class:      storage.ClassNvme,
					DeviceList: []string{pci1},
				},
			},
			expErr: errors.New("empty results from spdk binding format request"),
		},
		"binding format success": {
			mnc: spdk.MockNvmeCfg{
				FormatRes: []*spdk.FormatResult{
					{CtrlrPCIAddr: pci1, NsID: 1},
				},
			},
			req: storage.BdevFormatRequest{
				Properties: storage.BdevTierProperties{
					Class:      storage.ClassNvme,
					DeviceList: []string{pci1},
				},
			},
			expResp: &storage.BdevFormatResponse{
				DeviceResponses: map[string]*storage.BdevDeviceFormatResponse{
					pci1: {
						Formatted: true,
					},
				},
			},
			expInitOpts: []*spdk.EnvOptions{
				{PCIAllowList: []string{pci1}},
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
			req: storage.BdevFormatRequest{
				Properties: storage.BdevTierProperties{
					Class:      storage.ClassNvme,
					DeviceList: []string{pci1, pci2, pci3},
				},
			},
			expResp: &storage.BdevFormatResponse{
				DeviceResponses: storage.BdevDeviceFormatResponses{
					pci1: &storage.BdevDeviceFormatResponse{
						Formatted: true,
					},
					pci2: &storage.BdevDeviceFormatResponse{
						Formatted: true,
					},
					pci3: &storage.BdevDeviceFormatResponse{
						Formatted: true,
					},
				},
			},
			expInitOpts: []*spdk.EnvOptions{
				{PCIAllowList: []string{pci1, pci2, pci3}},
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
			req: storage.BdevFormatRequest{
				Properties: storage.BdevTierProperties{
					Class:      storage.ClassNvme,
					DeviceList: []string{pci1, pci2, pci3},
				},
			},
			expResp: &storage.BdevFormatResponse{
				DeviceResponses: storage.BdevDeviceFormatResponses{
					pci1: &storage.BdevDeviceFormatResponse{
						Formatted: true,
					},
					pci2: &storage.BdevDeviceFormatResponse{
						Formatted: true,
					},
					pci3: &storage.BdevDeviceFormatResponse{
						Error: FaultFormatError(
							pci3,
							errors.Errorf(
								"failed to format namespaces [2] (namespace 2: %s)",
								errors.New("spdk format failed"))),
					},
				},
			},
			expInitOpts: []*spdk.EnvOptions{
				{PCIAllowList: []string{pci1, pci2, pci3}},
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
			req: storage.BdevFormatRequest{
				Properties: storage.BdevTierProperties{
					Class:      storage.ClassNvme,
					DeviceList: []string{pci1},
				},
			},
			expResp: &storage.BdevFormatResponse{
				DeviceResponses: storage.BdevDeviceFormatResponses{
					pci1: &storage.BdevDeviceFormatResponse{
						Formatted: true,
					},
				},
			},
			expInitOpts: []*spdk.EnvOptions{
				{PCIAllowList: []string{pci1}},
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
			req: storage.BdevFormatRequest{
				Properties: storage.BdevTierProperties{
					Class:      storage.ClassNvme,
					DeviceList: []string{pci1},
				},
			},
			expResp: &storage.BdevFormatResponse{
				DeviceResponses: storage.BdevDeviceFormatResponses{
					pci1: &storage.BdevDeviceFormatResponse{
						Error: FaultFormatError(
							pci1,
							errors.Errorf(
								"failed to format namespaces [1 2 3 4] (namespace 1: %s)",
								errors.New("spdk format failed"))),
					},
				},
			},
			expInitOpts: []*spdk.EnvOptions{
				{PCIAllowList: []string{pci1}},
			},
		},
		"binding format success; vmd enabled": {
			mnc: spdk.MockNvmeCfg{
				FormatRes: []*spdk.FormatResult{
					{CtrlrPCIAddr: vmdBackingAddr1, NsID: 1},
					{CtrlrPCIAddr: vmdBackingAddr2, NsID: 1},
				},
			},
			req: storage.BdevFormatRequest{
				Properties: storage.BdevTierProperties{
					Class:      storage.ClassNvme,
					DeviceList: []string{vmdAddr},
				},
				VMDEnabled: true,
				BdevCache: &storage.BdevScanResponse{
					Controllers: mockCtrlrsInclVMD(),
				},
			},
			expResp: &storage.BdevFormatResponse{
				DeviceResponses: map[string]*storage.BdevDeviceFormatResponse{
					vmdBackingAddr1: {Formatted: true},
					vmdBackingAddr2: {Formatted: true},
				},
			},
			expInitOpts: []*spdk.EnvOptions{
				{
					PCIAllowList: []string{vmdBackingAddr1, vmdBackingAddr2},
					EnableVMD:    true,
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			mei := &spdk.MockEnvImpl{Cfg: tc.mec}
			sr, _ := mockScriptRunner(t, log, nil)
			b := &spdkBackend{
				log: log,
				binding: &spdkWrapper{
					Env:  mei,
					Nvme: &spdk.MockNvmeImpl{Cfg: tc.mnc},
				},
				script: sr,
			}

			// output path would be set during config validate
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

			switch tc.req.Properties.Class {
			case storage.ClassFile:
				// verify empty files created for AIO class
				for _, testFile := range tc.req.Properties.DeviceList {
					if _, err := os.Stat(testFile); err != nil {
						t.Fatal(err)
					}
				}
			case storage.ClassNvme:
				if diff := cmp.Diff(tc.expInitOpts, mei.CallOpts, defCmpOpts()...); diff != "" {
					t.Fatalf("\nunexpected output (-want, +got):\n%s\n", diff)
				}
			}
		})
	}
}

func TestBackend_writeNvmeConfig(t *testing.T) {
	for name, tc := range map[string]struct {
		req      storage.BdevWriteConfigRequest
		writeErr error
		expErr   error
		expCall  *storage.BdevWriteConfigRequest
	}{
		"write conf success": {
			req: storage.BdevWriteConfigRequest{
				TierProps: []storage.BdevTierProperties{
					{
						Class: storage.ClassDcpm,
					},
					{
						Class:      storage.ClassNvme,
						DeviceList: []string{common.MockPCIAddr(1)},
					},
				},
			},
			expCall: &storage.BdevWriteConfigRequest{
				TierProps: []storage.BdevTierProperties{
					{
						Class: storage.ClassDcpm,
					},
					{
						Class:      storage.ClassNvme,
						DeviceList: []string{common.MockPCIAddr(1)},
					},
				},
			},
		},
		"write conf failure": {
			req: storage.BdevWriteConfigRequest{
				TierProps: []storage.BdevTierProperties{
					{
						Class: storage.ClassDcpm,
					},
					{
						Class:      storage.ClassNvme,
						DeviceList: []string{common.MockPCIAddr(1)},
					},
				},
			},
			writeErr: errors.New("test"),
			expCall: &storage.BdevWriteConfigRequest{
				TierProps: []storage.BdevTierProperties{
					{
						Class: storage.ClassDcpm,
					},
					{
						Class:      storage.ClassNvme,
						DeviceList: []string{common.MockPCIAddr(1)},
					},
				},
			},
			expErr: errors.New("test"),
		},
		"write conf success; vmd enabled": {
			req: storage.BdevWriteConfigRequest{
				VMDEnabled: true,
				TierProps: []storage.BdevTierProperties{
					{
						Class: storage.ClassDcpm,
					},
					{
						Class:      storage.ClassNvme,
						DeviceList: []string{vmdAddr},
					},
				},
				BdevCache: &storage.BdevScanResponse{
					Controllers: mockCtrlrsInclVMD(),
				},
			},
			expCall: &storage.BdevWriteConfigRequest{
				VMDEnabled: true,
				TierProps: []storage.BdevTierProperties{
					{
						Class:      storage.ClassNvme,
						DeviceList: []string{vmdBackingAddr1, vmdBackingAddr2},
					},
				},
				BdevCache: &storage.BdevScanResponse{
					Controllers: mockCtrlrsInclVMD(),
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			sr, _ := mockScriptRunner(t, log, nil)
			b := newBackend(log, sr)

			var gotCall *storage.BdevWriteConfigRequest
			gotErr := b.writeNVMEConf(tc.req, func(l logging.Logger, r *storage.BdevWriteConfigRequest) error {
				l.Debugf("req: %+v", r)
				gotCall = r
				return tc.writeErr
			})
			if diff := cmp.Diff(tc.expCall, gotCall, defCmpOpts()...); diff != "" {
				t.Fatalf("\nunexpected request made (-want, +got):\n%s\n", diff)
			}
			common.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
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

func TestBackend_Prepare(t *testing.T) {
	const (
		testNrHugePages       = 42
		nonexistentTargetUser = "nonexistentTargetUser"
		username              = "bob"
	)
	var (
		testPCIAllowList = fmt.Sprintf("%s %s %s", common.MockPCIAddr(1), common.MockPCIAddr(2),
			common.MockPCIAddr(3))
		testPCIBlockList = fmt.Sprintf("%s %s", common.MockPCIAddr(4), common.MockPCIAddr(3))
	)

	for name, tc := range map[string]struct {
		reset          bool
		req            storage.BdevPrepareRequest
		mbc            *MockBackendConfig
		userLookupRet  *user.User
		userLookupErr  error
		vmdDetectRet   []string
		vmdDetectErr   error
		hpCleanErr     error
		expScriptCalls *[]scriptCall
		expErr         error
	}{
		"unknown target user": {
			req: storage.BdevPrepareRequest{
				TargetUser:            username,
				EnableVMD:             false,
				DisableCleanHugePages: true,
			},
			userLookupErr: errors.New("unknown user"),
			expErr:        errors.New("lookup on local host: unknown user"),
		},
		"prepare reset; defaults": {
			reset: true,
			req: storage.BdevPrepareRequest{
				TargetUser:            username,
				EnableVMD:             false,
				DisableCleanHugePages: true,
			},
			expScriptCalls: &[]scriptCall{
				{
					Env: []string{
						fmt.Sprintf("PATH=%s", os.Getenv("PATH")),
					},
					Args: []string{"reset"},
				},
			},
		},
		"prepare reset fails": {
			reset: true,
			req: storage.BdevPrepareRequest{
				TargetUser:            username,
				EnableVMD:             false,
				DisableCleanHugePages: true,
			},
			mbc: &MockBackendConfig{
				ResetErr: errors.New("reset failed"),
			},
			expErr: errors.New("reset failed"),
		},
		"prepare setup; defaults": {
			req: storage.BdevPrepareRequest{
				TargetUser:            username,
				EnableVMD:             false,
				DisableCleanHugePages: true,
			},
			expScriptCalls: &[]scriptCall{
				{
					Env: []string{
						fmt.Sprintf("PATH=%s", os.Getenv("PATH")),
						fmt.Sprintf("%s=%d", nrHugepagesEnv, defaultNrHugepages),
						fmt.Sprintf("%s=%s", targetUserEnv, username),
					},
				},
			},
		},
		"prepare setup; user-specified values": {
			req: storage.BdevPrepareRequest{
				HugePageCount:         testNrHugePages,
				TargetUser:            username,
				PCIAllowList:          testPCIAllowList,
				DisableVFIO:           true,
				EnableVMD:             false,
				DisableCleanHugePages: true,
			},
			expScriptCalls: &[]scriptCall{
				{
					Env: []string{
						fmt.Sprintf("PATH=%s", os.Getenv("PATH")),
						fmt.Sprintf("%s=%d", nrHugepagesEnv, testNrHugePages),
						fmt.Sprintf("%s=%s", targetUserEnv, username),
						fmt.Sprintf("%s=%s", pciAllowListEnv, testPCIAllowList),
						fmt.Sprintf("%s=%s", driverOverrideEnv, vfioDisabledDriver),
					},
				},
			},
		},
		"prepare setup; blocklist": {
			req: storage.BdevPrepareRequest{
				HugePageCount:         testNrHugePages,
				TargetUser:            username,
				PCIBlockList:          testPCIBlockList,
				EnableVMD:             false,
				DisableCleanHugePages: true,
			},
			expScriptCalls: &[]scriptCall{
				{
					Env: []string{
						fmt.Sprintf("PATH=%s", os.Getenv("PATH")),
						fmt.Sprintf("%s=%d", nrHugepagesEnv, testNrHugePages),
						fmt.Sprintf("%s=%s", targetUserEnv, username),
						fmt.Sprintf("%s=%s", pciBlockListEnv, testPCIBlockList),
					},
				},
			},
		},
		"prepare setup; blocklist allowlist": {
			req: storage.BdevPrepareRequest{
				HugePageCount:         testNrHugePages,
				TargetUser:            username,
				PCIBlockList:          testPCIBlockList,
				PCIAllowList:          testPCIAllowList,
				EnableVMD:             false,
				DisableCleanHugePages: true,
			},
			expScriptCalls: &[]scriptCall{
				{
					Env: []string{
						fmt.Sprintf("PATH=%s", os.Getenv("PATH")),
						fmt.Sprintf("%s=%d", nrHugepagesEnv, testNrHugePages),
						fmt.Sprintf("%s=%s", targetUserEnv, username),
						fmt.Sprintf("%s=%s", pciAllowListEnv, testPCIAllowList),
						fmt.Sprintf("%s=%s", pciBlockListEnv, testPCIBlockList),
					},
				},
			},
		},
		"prepare setup; fails": {
			req: storage.BdevPrepareRequest{
				TargetUser:            username,
				EnableVMD:             false,
				DisableCleanHugePages: true,
			},
			mbc: &MockBackendConfig{
				PrepareErr: errors.New("prepare failed"),
			},
			expErr: errors.New("prepare failed"),
		},
		"prepare setup; vmd enabled": {
			req: storage.BdevPrepareRequest{
				HugePageCount:         testNrHugePages,
				TargetUser:            username,
				DisableCleanHugePages: true,
				EnableVMD:             true,
			},
			vmdDetectRet: []string{common.MockPCIAddr(1), common.MockPCIAddr(2)},
			expScriptCalls: &[]scriptCall{
				{
					Env: []string{
						fmt.Sprintf("PATH=%s", os.Getenv("PATH")),
						fmt.Sprintf("%s=%d", nrHugepagesEnv, testNrHugePages),
						fmt.Sprintf("%s=%s", targetUserEnv, username),
						fmt.Sprintf("%s=%s", pciAllowListEnv, fmt.Sprintf("%s %s",
							common.MockPCIAddr(1), common.MockPCIAddr(2))),
					},
				},
				{
					Env: []string{
						fmt.Sprintf("PATH=%s", os.Getenv("PATH")),
						fmt.Sprintf("%s=%d", nrHugepagesEnv, testNrHugePages),
						fmt.Sprintf("%s=%s", targetUserEnv, username),
					},
				},
			},
		},
		"prepare setup; vmd enabled; vmd detect failed": {
			req: storage.BdevPrepareRequest{
				HugePageCount:         testNrHugePages,
				TargetUser:            username,
				DisableCleanHugePages: true,
				EnableVMD:             true,
			},
			vmdDetectRet: []string{common.MockPCIAddr(1), common.MockPCIAddr(2)},
			vmdDetectErr: errors.New("vmd detect failed"),
			expScriptCalls: &[]scriptCall{
				{
					Env: []string{
						fmt.Sprintf("PATH=%s", os.Getenv("PATH")),
						fmt.Sprintf("%s=%d", nrHugepagesEnv, testNrHugePages),
						fmt.Sprintf("%s=%s", targetUserEnv, username),
					},
				},
			},
			expErr: errors.New("vmd detect failed"),
		},
		"prepare setup; vmd enabled; no vmd devices": {
			req: storage.BdevPrepareRequest{
				HugePageCount:         testNrHugePages,
				TargetUser:            username,
				DisableCleanHugePages: true,
				EnableVMD:             true,
			},
			vmdDetectRet: []string{},
			expScriptCalls: &[]scriptCall{
				{
					Env: []string{
						fmt.Sprintf("PATH=%s", os.Getenv("PATH")),
						fmt.Sprintf("%s=%d", nrHugepagesEnv, testNrHugePages),
						fmt.Sprintf("%s=%s", targetUserEnv, username),
					},
				},
			},
		},
		"prepare setup; vmd enabled; vmd device allowed": {
			req: storage.BdevPrepareRequest{
				HugePageCount:         testNrHugePages,
				TargetUser:            username,
				PCIAllowList:          testPCIAllowList,
				DisableCleanHugePages: true,
				EnableVMD:             true,
			},
			vmdDetectRet: []string{common.MockPCIAddr(3), common.MockPCIAddr(4)},
			expScriptCalls: &[]scriptCall{
				{
					Env: []string{
						fmt.Sprintf("PATH=%s", os.Getenv("PATH")),
						fmt.Sprintf("%s=%d", nrHugepagesEnv, testNrHugePages),
						fmt.Sprintf("%s=%s", targetUserEnv, username),
						fmt.Sprintf("%s=%s", pciAllowListEnv, common.MockPCIAddr(3)),
					},
				},
				{
					Env: []string{
						fmt.Sprintf("PATH=%s", os.Getenv("PATH")),
						fmt.Sprintf("%s=%d", nrHugepagesEnv, testNrHugePages),
						fmt.Sprintf("%s=%s", targetUserEnv, username),
						fmt.Sprintf("%s=%s", pciAllowListEnv, testPCIAllowList),
					},
				},
			},
		},
		"prepare setup; vmd enabled; vmd device blocked": {
			req: storage.BdevPrepareRequest{
				HugePageCount:         testNrHugePages,
				TargetUser:            username,
				PCIBlockList:          testPCIBlockList,
				DisableCleanHugePages: true,
				EnableVMD:             true,
			},
			vmdDetectRet: []string{common.MockPCIAddr(3), common.MockPCIAddr(5)},
			expScriptCalls: &[]scriptCall{
				{
					Env: []string{
						fmt.Sprintf("PATH=%s", os.Getenv("PATH")),
						fmt.Sprintf("%s=%d", nrHugepagesEnv, testNrHugePages),
						fmt.Sprintf("%s=%s", targetUserEnv, username),
						fmt.Sprintf("%s=%s", pciAllowListEnv, common.MockPCIAddr(5)),
					},
				},
				{
					Env: []string{
						fmt.Sprintf("PATH=%s", os.Getenv("PATH")),
						fmt.Sprintf("%s=%d", nrHugepagesEnv, testNrHugePages),
						fmt.Sprintf("%s=%s", targetUserEnv, username),
						fmt.Sprintf("%s=%s", pciBlockListEnv, testPCIBlockList),
					},
				},
			},
		},
		"prepare setup; vmd enabled; vmd devices all blocked": {
			req: storage.BdevPrepareRequest{
				HugePageCount:         testNrHugePages,
				TargetUser:            username,
				PCIBlockList:          testPCIBlockList,
				DisableCleanHugePages: true,
				EnableVMD:             true,
			},
			vmdDetectRet: []string{common.MockPCIAddr(3), common.MockPCIAddr(4)},
			expScriptCalls: &[]scriptCall{
				{
					Env: []string{
						fmt.Sprintf("PATH=%s", os.Getenv("PATH")),
						fmt.Sprintf("%s=%d", nrHugepagesEnv, testNrHugePages),
						fmt.Sprintf("%s=%s", targetUserEnv, username),
						fmt.Sprintf("%s=%s", pciBlockListEnv, testPCIBlockList),
					},
				},
			},
		},
		"prepare setup; vmd enabled; vmd devices allowed and blocked": {
			req: storage.BdevPrepareRequest{
				HugePageCount:         testNrHugePages,
				TargetUser:            username,
				PCIAllowList:          testPCIAllowList,
				PCIBlockList:          testPCIBlockList,
				DisableCleanHugePages: true,
				EnableVMD:             true,
			},
			vmdDetectRet: []string{common.MockPCIAddr(3), common.MockPCIAddr(2)},
			expScriptCalls: &[]scriptCall{
				{
					Env: []string{
						fmt.Sprintf("PATH=%s", os.Getenv("PATH")),
						fmt.Sprintf("%s=%d", nrHugepagesEnv, testNrHugePages),
						fmt.Sprintf("%s=%s", targetUserEnv, username),
						fmt.Sprintf("%s=%s", pciAllowListEnv, common.MockPCIAddr(2)),
					},
				},
				{
					Env: []string{
						fmt.Sprintf("PATH=%s", os.Getenv("PATH")),
						fmt.Sprintf("%s=%d", nrHugepagesEnv, testNrHugePages),
						fmt.Sprintf("%s=%s", targetUserEnv, username),
						fmt.Sprintf("%s=%s", pciAllowListEnv, testPCIAllowList),
						fmt.Sprintf("%s=%s", pciBlockListEnv, testPCIBlockList),
					},
				},
			},
		},
		"prepare setup; huge page clean enabled": {
			req: storage.BdevPrepareRequest{
				HugePageCount:         testNrHugePages,
				TargetUser:            username,
				PCIAllowList:          testPCIAllowList,
				PCIBlockList:          testPCIBlockList,
				DisableCleanHugePages: false,
			},
			expScriptCalls: &[]scriptCall{
				{
					Env: []string{
						fmt.Sprintf("PATH=%s", os.Getenv("PATH")),
						fmt.Sprintf("%s=%d", nrHugepagesEnv, testNrHugePages),
						fmt.Sprintf("%s=%s", targetUserEnv, username),
						fmt.Sprintf("%s=%s", pciAllowListEnv, testPCIAllowList),
						fmt.Sprintf("%s=%s", pciBlockListEnv, testPCIBlockList),
					},
				},
			},
		},
		"prepare setup; huge page clean enabled; clean fail": {
			req: storage.BdevPrepareRequest{
				HugePageCount:         testNrHugePages,
				TargetUser:            username,
				PCIAllowList:          testPCIAllowList,
				PCIBlockList:          testPCIBlockList,
				DisableCleanHugePages: false,
			},
			hpCleanErr: errors.New("clean failed"),
			expScriptCalls: &[]scriptCall{
				{
					Env: []string{
						fmt.Sprintf("PATH=%s", os.Getenv("PATH")),
						fmt.Sprintf("%s=%d", nrHugepagesEnv, testNrHugePages),
						fmt.Sprintf("%s=%s", targetUserEnv, username),
						fmt.Sprintf("%s=%s", pciAllowListEnv, testPCIAllowList),
						fmt.Sprintf("%s=%s", pciBlockListEnv, testPCIBlockList),
					},
				},
			},
			expErr: errors.New("clean failed"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			sss, calls := mockScriptRunner(t, log, tc.mbc)
			b := newBackend(log, sss)

			if tc.userLookupRet == nil {
				tc.userLookupRet, _ = user.Current()
			}
			mockUserLookup := func(string) (*user.User, error) {
				return tc.userLookupRet, tc.userLookupErr
			}
			mockVmdDetect := func() ([]string, error) {
				return tc.vmdDetectRet, tc.vmdDetectErr
			}
			mockHpClean := func(string, string, string) error {
				return tc.hpCleanErr
			}

			var gotErr error
			if tc.reset {
				gotErr = b.Reset(tc.req)
			} else {
				_, gotErr = b.prepare(tc.req, mockUserLookup, mockVmdDetect, mockHpClean)
			}
			common.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expScriptCalls, calls); diff != "" {
				t.Fatalf("\nunexpected cmd env (-want, +got):\n%s\n", diff)
			}

		})
	}
}

func TestBackend_checkCfgBdevsExist(t *testing.T) {
	for name, tc := range map[string]struct {
		vmdEnabled    bool
		inControllers storage.NvmeControllers
		engineStorage map[uint32]*storage.Config
		expErr        error
	}{
		"empty cfg bdev list": {
			engineStorage: make(map[uint32]*storage.Config),
		},
		"addr in cfg bdev list; vmd disabled": {
			engineStorage: map[uint32]*storage.Config{
				0: {
					Tiers: storage.TierConfigs{
						storage.NewTierConfig().
							WithBdevClass(storage.ClassNvme.String()).
							WithBdevDeviceList(vmdAddr),
					},
				},
			},
			expErr: FaultBdevNotFound(vmdAddr),
		},
		"addr in cfg bdev list; vmd enabled": {
			vmdEnabled: true,
			engineStorage: map[uint32]*storage.Config{
				0: {
					Tiers: storage.TierConfigs{
						storage.NewTierConfig().
							WithBdevClass(storage.ClassNvme.String()).
							WithBdevDeviceList(vmdAddr),
					},
				},
			},
		},
		"no backing devices": {
			vmdEnabled: true,
			engineStorage: map[uint32]*storage.Config{
				0: {
					Tiers: storage.TierConfigs{
						storage.NewTierConfig().
							WithBdevClass(storage.ClassNvme.String()).
							WithBdevDeviceList("0000:d7:05.5"),
					},
				},
			},
			expErr: FaultBdevNotFound("0000:d7:05.5"),
		},
		"vmd and non vmd in scan; addr in cfg bdev list": {
			vmdEnabled: true,
			engineStorage: map[uint32]*storage.Config{
				0: {
					Tiers: storage.TierConfigs{
						storage.NewTierConfig().
							WithBdevClass(storage.ClassNvme.String()).
							WithBdevDeviceList("0000:8a:00.0", "0000:8d:00.0",
								vmdAddr),
					},
				},
			},
		},
		"vmd and non vmd in scan; addr in cfg bdev list; multiple io servers": {
			vmdEnabled: true,
			inControllers: append(mockCtrlrsInclVMD(),
				&storage.NvmeController{PciAddr: "d70505:01:00.0"},
				&storage.NvmeController{PciAddr: "d70505:02:00.0"}),
			engineStorage: map[uint32]*storage.Config{
				0: {
					Tiers: storage.TierConfigs{
						storage.NewTierConfig().
							WithBdevClass(storage.ClassNvme.String()).
							WithBdevDeviceList("0000:90:00.0", "0000:d8:00.0",
								"0000:d7:05.5"),
					},
				},
				1: {
					Tiers: storage.TierConfigs{
						storage.NewTierConfig().
							WithBdevClass(storage.ClassNvme.String()).
							WithBdevDeviceList("0000:8a:00.0", "0000:8d:00.0",
								vmdAddr),
					},
				},
			},
		},
		"unexpected scan": {
			inControllers: storage.MockNvmeControllers(3),
			engineStorage: map[uint32]*storage.Config{
				0: {
					Tiers: storage.TierConfigs{
						storage.NewTierConfig().
							WithBdevClass(storage.ClassNvme.String()).
							WithBdevDeviceList("0000:90:00.0", "0000:d8:00.0",
								"0000:d7:05.5"),
					},
				},
				1: {
					Tiers: storage.TierConfigs{
						storage.NewTierConfig().
							WithBdevClass(storage.ClassNvme.String()).
							WithBdevDeviceList("0000:8a:00.0", "0000:8d:00.0",
								vmdAddr),
					},
				},
			},
			expErr: errors.New("not found"), // engine order not deterministic
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.inControllers == nil {
				tc.inControllers = mockCtrlrsInclVMD()
			}

			gotErr := checkCfgBdevsExist(log, tc.inControllers, tc.engineStorage, tc.vmdEnabled)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
		})
	}
}
