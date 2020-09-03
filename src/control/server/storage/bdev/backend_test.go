//
// (C) Copyright 2018-2020 Intel Corporation.
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
package bdev

import (
	"encoding/json"
	"testing"

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

func mockSpdkController(varIdx ...int32) spdk.Controller {
	native := storage.MockNvmeController(varIdx...)

	s := new(spdk.Controller)
	if err := convertTypes(native, s); err != nil {
		panic(err)
	}

	return *s
}

func mockSpdkNamespace(varIdx ...int32) spdk.Namespace {
	native := storage.MockNvmeNamespace(varIdx...)

	s := new(spdk.Namespace)
	if err := convertTypes(native, s); err != nil {
		panic(err)
	}

	return *s
}

func mockSpdkDeviceHealth(varIdx ...int32) spdk.DeviceHealth {
	native := storage.MockNvmeDeviceHealth(varIdx...)

	s := new(spdk.DeviceHealth)
	if err := convertTypes(native, s); err != nil {
		panic(err)
	}

	return *s
}

func TestBdevBackendGetController(t *testing.T) {
	type input struct {
		pciAddr         string
		spdkControllers []spdk.Controller
	}

	spdkNormalNoHealth := mockSpdkController()
	spdkNormalNoHealth.HealthStats = nil
	scNormalNoHealth := storage.MockNvmeController()
	scNormalNoHealth.HealthStats = nil

	for name, tc := range map[string]struct {
		input  input
		expSc  *storage.NvmeController
		expErr error
	}{
		"empty input": {
			input:  input{},
			expErr: FaultBadPCIAddr(""),
		},
		"wrong pciAddr": {
			input: input{
				pciAddr:         "abc123",
				spdkControllers: []spdk.Controller{mockSpdkController()},
			},
			expErr: FaultPCIAddrNotFound("abc123"),
		},
		"correct pciAddr": {
			input: input{
				pciAddr:         mockSpdkController().PCIAddr,
				spdkControllers: []spdk.Controller{mockSpdkController()},
			},
			expSc: storage.MockNvmeController(),
		},
		"missing health stats": {
			input: input{
				pciAddr:         spdkNormalNoHealth.PCIAddr,
				spdkControllers: []spdk.Controller{spdkNormalNoHealth},
			},
			expSc: scNormalNoHealth,
		},
		"two controllers select first": {
			input: input{
				pciAddr:         mockSpdkController(1).PCIAddr,
				spdkControllers: []spdk.Controller{mockSpdkController(1), mockSpdkController(2)},
			},
			expSc: storage.MockNvmeController(1),
		},
		"two controllers select second": {
			input: input{
				pciAddr:         mockSpdkController(2).PCIAddr,
				spdkControllers: []spdk.Controller{mockSpdkController(1), mockSpdkController(2)},
			},
			expSc: storage.MockNvmeController(2),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotSc, gotErr := getController(tc.input.pciAddr, tc.input.spdkControllers)

			common.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expSc, gotSc, defCmpOpts()...); diff != "" {
				t.Fatalf("\nunexpected output (-want, +got):\n%s\n", diff)
			}
		})
	}
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

func TestBdevBackendScan(t *testing.T) {
	ctrlr1 := storage.MockNvmeController(1)
	nativeCtrlr1 := new(spdk.Controller)
	if err := convert(ctrlr1, nativeCtrlr1); err != nil {
		t.Fatal(err)
	}

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
			expResp: &ScanResponse{Controllers: storage.NvmeControllers{}},
		},
		"binding scan success": {
			mnc: spdk.MockNvmeCfg{
				DiscoverCtrlrs: []spdk.Controller{*nativeCtrlr1},
			},
			req: ScanRequest{},
			expResp: &ScanResponse{
				Controllers: storage.NvmeControllers{
					ctrlr1,
				},
			},
		},
		"binding scan filtered in": {
			mnc: spdk.MockNvmeCfg{
				DiscoverCtrlrs: []spdk.Controller{*nativeCtrlr1},
			},
			req: ScanRequest{DeviceList: []string{ctrlr1.PciAddr}},
			expResp: &ScanResponse{
				Controllers: storage.NvmeControllers{
					ctrlr1,
				},
			},
		},
		"binding scan filtered out": {
			mnc: spdk.MockNvmeCfg{
				DiscoverCtrlrs: []spdk.Controller{*nativeCtrlr1},
			},
			req:     ScanRequest{DeviceList: []string{"0000:ff:ff.f"}},
			expResp: &ScanResponse{Controllers: storage.NvmeControllers{}},
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

func TestBdevBackendFormat(t *testing.T) {
	pci1 := storage.MockNvmeController(1).PciAddr
	pci2 := storage.MockNvmeController(2).PciAddr
	pci3 := storage.MockNvmeController(3).PciAddr

	for name, tc := range map[string]struct {
		req     FormatRequest
		mec     spdk.MockEnvCfg
		mnc     spdk.MockNvmeCfg
		expResp *FormatResponse
		expErr  error
	}{
		"empty device list": {
			req: FormatRequest{
				Class: storage.BdevClassNvme,
			},
			expErr: errors.New("empty pci address list in format request"),
		},
		"unknown device class": {
			req: FormatRequest{
				Class:      storage.BdevClass("whoops"),
				DeviceList: []string{pci1},
			},
			expErr: FaultFormatUnknownClass("whoops"),
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
					pci1: &DeviceFormatResponse{
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
		"kdev": {
			req: FormatRequest{
				Class:      storage.BdevClassKdev,
				DeviceList: []string{"foo"},
			},
			expResp: &FormatResponse{
				DeviceResponses: map[string]*DeviceFormatResponse{
					"foo": &DeviceFormatResponse{
						Formatted: true,
					},
				},
			},
		},
		"file": {
			req: FormatRequest{
				Class:      storage.BdevClassFile,
				DeviceList: []string{"foo"},
			},
			expResp: &FormatResponse{
				DeviceResponses: map[string]*DeviceFormatResponse{
					"foo": &DeviceFormatResponse{
						Formatted: true,
					},
				},
			},
		},
		"malloc": {
			req: FormatRequest{
				Class:      storage.BdevClassMalloc,
				DeviceList: []string{"foo"},
			},
			expResp: &FormatResponse{
				DeviceResponses: map[string]*DeviceFormatResponse{
					"foo": &DeviceFormatResponse{
						Formatted: true,
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			b := backendWithMockBinding(log, tc.mec, tc.mnc)

			gotResp, gotErr := b.Format(tc.req)
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

func TestBdevBackendUpdate(t *testing.T) {
	numCtrlrs := 4
	controllers := make([]spdk.Controller, 0, numCtrlrs)
	for i := 0; i < numCtrlrs; i++ {
		c := mockSpdkController(int32(i))
		controllers = append(controllers, c)
	}

	for name, tc := range map[string]struct {
		pciAddr string
		mec     spdk.MockEnvCfg
		mnc     spdk.MockNvmeCfg
		expErr  error
	}{
		"init failed": {
			pciAddr: controllers[0].PCIAddr,
			mec: spdk.MockEnvCfg{
				InitErr: errors.New("spdk init says no"),
			},
			mnc: spdk.MockNvmeCfg{
				DiscoverCtrlrs: controllers,
			},
			expErr: errors.New("spdk init says no"),
		},
		"not found": {
			pciAddr: "NotReal",
			mnc: spdk.MockNvmeCfg{
				DiscoverCtrlrs: controllers,
			},
			expErr: FaultPCIAddrNotFound("NotReal"),
		},
		"binding update fail": {
			pciAddr: controllers[0].PCIAddr,
			mnc: spdk.MockNvmeCfg{
				DiscoverCtrlrs: controllers,
				UpdateErr:      errors.New("spdk says no"),
			},
			expErr: errors.New("spdk says no"),
		},
		"binding update success": {
			pciAddr: controllers[0].PCIAddr,
			mnc: spdk.MockNvmeCfg{
				DiscoverCtrlrs: controllers,
			},
			expErr: nil,
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
