//
// (C) Copyright 2018-2019 Intel Corporation.
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
package server

import (
	"testing"

	"github.com/daos-stack/daos/src/control/common"
	. "github.com/daos-stack/daos/src/control/common/proto/ctl"
	. "github.com/daos-stack/daos/src/control/common/storage"
	. "github.com/daos-stack/daos/src/control/lib/spdk"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
)

var nvmeFormatCalls []string // record calls to nvme.Format()

// MockController is a mock NVMe SSD controller of type exported from go-spdk.
func MockController() Controller {
	c := common.MockControllerPB()
	return Controller{
		Model:    c.Model,
		Serial:   c.Serial,
		PCIAddr:  c.Pciaddr,
		FWRev:    c.Fwrev,
		SocketID: c.Socketid,
	}
}

// NewMockController specifies customer details fr a mock NVMe SSD controller.
func NewMockController(
	pciaddr string, fwrev string, model string, serial string, socketID int32,
) Controller {
	return Controller{
		Model:    model,
		Serial:   serial,
		PCIAddr:  pciaddr,
		FWRev:    fwrev,
		SocketID: socketID,
	}
}

// MockNamespace is a mock NVMe SSD namespace of type exported from go-spdk.
func MockNamespace(ctrlr *Controller) Namespace {
	n := common.MockNamespacePB()
	return Namespace{
		ID:           n.Id,
		Size:         n.Size,
		CtrlrPciAddr: ctrlr.PCIAddr,
	}
}

// MockDeviceHealth is a mock NVMe SSD device health of type exported from go-spdk.
func MockDeviceHealth(ctrlr *Controller) DeviceHealth {
	h := common.MockDeviceHealthPB()
	return DeviceHealth{
		Temp:            h.Temp,
		TempWarnTime:    h.Tempwarntime,
		TempCritTime:    h.Tempcrittime,
		CtrlBusyTime:    h.Ctrlbusytime,
		PowerCycles:     h.Powercycles,
		PowerOnHours:    h.Poweronhours,
		UnsafeShutdowns: h.Unsafeshutdowns,
		MediaErrors:     h.Mediaerrors,
		ErrorLogEntries: h.Errorlogentries,
		TempWarn:        h.Tempwarn,
		AvailSpareWarn:  h.Availsparewarn,
		ReliabilityWarn: h.Reliabilitywarn,
		ReadOnlyWarn:    h.Readonlywarn,
		VolatileWarn:    h.Volatilewarn,
		CtrlrPciAddr:    ctrlr.PCIAddr,
	}
}

// mock external interface implementations for daos/src/control/lib/spdk package
type mockSpdkEnv struct {
	initRet error // ENV interface InitSPDKEnv() return value
}

func (m *mockSpdkEnv) InitSPDKEnv(int) error { return m.initRet }

func newMockSpdkEnv(initRet error) ENV { return &mockSpdkEnv{initRet} }

func defaultMockSpdkEnv() ENV { return newMockSpdkEnv(nil) }

// mock external interface implementations for daos/src/control/lib/nvme package
type mockSpdkNvme struct {
	log          logging.Logger
	initCtrlrs   []Controller
	initNss      []Namespace
	initHealth   []DeviceHealth
	discoverRet  error // NVME interface Discover() return value
	devFormatRet error // NVME interface Format() return value
}

// Discover mock implementation returns mock lists of devices
func (m *mockSpdkNvme) Discover() ([]Controller, []Namespace, []DeviceHealth, error) {
	return m.initCtrlrs, m.initNss, m.initHealth, m.discoverRet
}

// Format mock implementation records calls on devices with given pci address
func (m *mockSpdkNvme) Format(pciAddr string) ([]Controller, []Namespace, error) {
	if m.devFormatRet == nil {
		nvmeFormatCalls = append(nvmeFormatCalls, pciAddr)
	}

	return m.initCtrlrs, m.initNss, m.devFormatRet
}

func (m *mockSpdkNvme) Cleanup() {}

func newMockSpdkNvme(log logging.Logger, ctrlrs []Controller, nss []Namespace, dh []DeviceHealth, discoverRet error, devFormatRet error) NVME {
	return &mockSpdkNvme{log, ctrlrs, nss, dh, discoverRet, devFormatRet}
}

func defaultMockSpdkNvme(log logging.Logger) NVME {
	c := MockController()

	return newMockSpdkNvme(log, []Controller{c}, []Namespace{MockNamespace(&c)},
		[]DeviceHealth{MockDeviceHealth(&c)}, nil, nil)
}

// mock external interface implementations for spdk setup script
// TODO: provide capability to return values from mock
type mockSpdkSetup struct{}

func (m *mockSpdkSetup) prep(int, string, string) error { return nil }
func (m *mockSpdkSetup) reset() error                   { return nil }

// mockNvmeStorage factory
func newMockNvmeStorage(
	log logging.Logger, ext External,
	spdkEnv ENV, spdkNvme NVME, inited bool) *nvmeStorage {

	return &nvmeStorage{
		log:         log,
		ext:         ext,
		env:         spdkEnv,
		nvme:        spdkNvme,
		spdk:        &mockSpdkSetup{},
		initialized: inited,
	}
}

// defaultMockNvmeStorage factory
func defaultMockNvmeStorage(log logging.Logger, ext External) *nvmeStorage {
	return newMockNvmeStorage(
		log, ext,
		defaultMockSpdkEnv(),
		defaultMockSpdkNvme(log),
		false) // Discover will not fetch when initialised is true
}

func TestDiscoverNvmeSingle(t *testing.T) {
	tests := []struct {
		inited          bool
		spdkInitEnvRet  error // return value from go-spdk pkg
		spdkDiscoverRet error // return value from go-spdk pkg
		numa            int32 // NUMA socket ID of NVMe ctrlr
		errMsg          string
	}{
		{
			inited: true,
		},
		{},
		{
			spdkDiscoverRet: errors.New("spdk example failure"),
			errMsg:          msgSpdkDiscoverFail + ": spdk example failure",
		},
		{
			spdkInitEnvRet: errors.New("spdk example failure"),
			errMsg:         msgSpdkInitFail + ": spdk example failure",
		},
		{
			numa: 1,
		},
	}

	c := MockController()
	pbC := common.MockControllerPB()

	for _, tt := range tests {
		log, buf := logging.NewTestLogger(t.Name())
		defer common.ShowBufferOnFailure(t, buf)

		c.SocketID = tt.numa
		pbC.Socketid = tt.numa

		sn := newMockNvmeStorage(
			log, &mockExt{},
			newMockSpdkEnv(tt.spdkInitEnvRet),
			newMockSpdkNvme(log, []Controller{c}, []Namespace{MockNamespace(&c)},
				[]DeviceHealth{MockDeviceHealth(&c)}, tt.spdkDiscoverRet, nil),
			tt.inited)

		if err := sn.Discover(); err != nil {
			if tt.errMsg != "" {
				common.AssertEqual(t, err.Error(), tt.errMsg, "")
				continue
			}
			t.Fatal(err)
		}

		if tt.inited {
			common.AssertEqual(t, sn.controllers, NvmeControllers(nil),
				"unexpected list of protobuf format controllers")
			continue
		}

		common.AssertEqual(t, sn.controllers, NvmeControllers{pbC},
			"unexpected list of protobuf format controllers")
	}
}

// Verify correct mapping of namespaces to multiple controllers
func TestDiscoverNvmeMulti(t *testing.T) {
	mCs := []Controller{
		{PCIAddr: "1.2.3.4.5", FWRev: "1.0.0"},
		{PCIAddr: "1.2.3.4.6", FWRev: "1.0.0"},
	}
	ns0 := Namespace{ID: 0, Size: 100, CtrlrPciAddr: "1.2.3.4.5"}
	ns1 := Namespace{ID: 1, Size: 200, CtrlrPciAddr: "1.2.3.4.6"}
	ns2 := Namespace{ID: 2, Size: 100, CtrlrPciAddr: "1.2.3.4.5"}
	ns3 := Namespace{ID: 3, Size: 200, CtrlrPciAddr: "1.2.3.4.6"}
	dh0 := DeviceHealth{Temp: 300, PowerOnHours: 1000, UnsafeShutdowns: 1, CtrlrPciAddr: "1.2.3.4.5"}
	dh1 := DeviceHealth{Temp: 500, PowerOnHours: 500, UnsafeShutdowns: 2, CtrlrPciAddr: "1.2.3.4.6"}
	dh2 := DeviceHealth{Temp: 300, PowerOnHours: 1000, UnsafeShutdowns: 1, CtrlrPciAddr: "1.2.3.4.7"}
	dh3 := DeviceHealth{Temp: 500, PowerOnHours: 500, UnsafeShutdowns: 2, CtrlrPciAddr: "1.2.3.4.8"}

	for name, tc := range map[string]struct {
		nss         []Namespace
		dh          []DeviceHealth
		expPbCtrlrs NvmeControllers
	}{
		"no namespaces or health statistics": {
			[]Namespace{},
			[]DeviceHealth{},
			NvmeControllers{
				&NvmeController{Pciaddr: mCs[0].PCIAddr, Fwrev: mCs[0].FWRev},
				&NvmeController{Pciaddr: mCs[1].PCIAddr, Fwrev: mCs[1].FWRev},
			},
		},
		"matching namespace and health statistics": {
			[]Namespace{ns0, ns1},
			[]DeviceHealth{dh0, dh1},
			NvmeControllers{
				&NvmeController{
					Pciaddr: mCs[0].PCIAddr, Fwrev: mCs[0].FWRev,
					Namespaces: NvmeNamespaces{{Id: ns0.ID, Size: ns0.Size}},
					Healthstats: &NvmeController_Health{
						Temp: dh0.Temp, Poweronhours: dh0.PowerOnHours,
						Unsafeshutdowns: dh0.UnsafeShutdowns,
					},
				},
				&NvmeController{
					Pciaddr: mCs[1].PCIAddr, Fwrev: mCs[1].FWRev,
					Namespaces: NvmeNamespaces{{Id: ns1.ID, Size: ns1.Size}},
					Healthstats: &NvmeController_Health{
						Temp: dh1.Temp, Poweronhours: dh1.PowerOnHours,
						Unsafeshutdowns: dh1.UnsafeShutdowns,
					},
				},
			},
		},
		"multiple namespaces and no matching health statistics": {
			[]Namespace{ns0, ns1, ns2, ns3},
			[]DeviceHealth{dh2, dh3},
			NvmeControllers{
				&NvmeController{
					Pciaddr: mCs[0].PCIAddr, Fwrev: mCs[0].FWRev,
					Namespaces: NvmeNamespaces{
						{Id: ns0.ID, Size: ns0.Size},
						{Id: ns2.ID, Size: ns2.Size},
					},
				},
				&NvmeController{
					Pciaddr: mCs[1].PCIAddr, Fwrev: mCs[1].FWRev,
					Namespaces: NvmeNamespaces{
						{Id: ns1.ID, Size: ns1.Size},
						{Id: ns3.ID, Size: ns3.Size},
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			sn := newMockNvmeStorage(log, &mockExt{}, defaultMockSpdkEnv(),
				newMockSpdkNvme(log, mCs, tc.nss, tc.dh, nil, nil),
				false)

			if err := sn.Discover(); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(tc.expPbCtrlrs, sn.controllers); diff != "" {
				t.Fatalf("unexpected controller results (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestFormatNvme(t *testing.T) {
	pciAddr := "0000:81:00.0"
	model := "ABC"
	serial := "123ABC"
	fwRev := "1.0.0"
	newDefaultCtrlrs := func() NvmeControllers {
		return NvmeControllers{
			common.NewMockControllerPB(pciAddr, fwRev, model, serial,
				NvmeNamespaces(nil), nil),
		}
	}

	tests := []struct {
		formatted    bool
		devFormatRet error
		pciAddrs     []string
		expResults   NvmeControllerResults
		expCtrlrs    NvmeControllers
		desc         string
	}{
		{
			formatted:    false,
			devFormatRet: nil,
			pciAddrs:     []string{},
			expResults: NvmeControllerResults{
				{
					Pciaddr: "",
					State: &ResponseState{
						Status: ResponseStatus_CTL_SUCCESS,
						Info:   msgBdevNoDevs,
					},
				},
			},
			expCtrlrs: newDefaultCtrlrs(),
			desc:      "no devices",
		},
		{
			formatted:    true,
			devFormatRet: nil,
			pciAddrs:     []string{},
			expResults: NvmeControllerResults{
				{
					Pciaddr: "",
					State: &ResponseState{
						Status: ResponseStatus_CTL_ERR_APP,
						Error:  msgBdevAlreadyFormatted,
					},
				},
			},
			expCtrlrs: newDefaultCtrlrs(),
			desc:      "already formatted",
		},
		{
			formatted:    false,
			devFormatRet: nil,
			pciAddrs:     []string{""},
			expResults: NvmeControllerResults{
				{
					Pciaddr: "",
					State: &ResponseState{
						Status: ResponseStatus_CTL_ERR_CONF,
						Error:  msgBdevEmpty,
					},
				},
			},
			expCtrlrs: newDefaultCtrlrs(),
			desc:      "empty device string",
		},
		{
			formatted:    false,
			devFormatRet: nil,
			pciAddrs:     []string{"0000:81:00.0"},
			expResults: NvmeControllerResults{
				{
					Pciaddr: "0000:81:00.0",
					State:   new(ResponseState),
				},
			},
			expCtrlrs: newDefaultCtrlrs(),
			desc:      "single device",
		},
		{
			formatted:    false,
			devFormatRet: nil,
			pciAddrs:     []string{"0000:83:00.0"},
			expResults: NvmeControllerResults{
				{
					Pciaddr: "0000:83:00.0",
					State: &ResponseState{
						Status: ResponseStatus_CTL_ERR_NVME,
						Error:  "0000:83:00.0: " + msgBdevNotFound,
					},
				},
			},
			expCtrlrs: newDefaultCtrlrs(),
			desc:      "single device not discovered",
		},
		{
			formatted:    false,
			devFormatRet: nil,
			pciAddrs:     []string{"0000:81:00.0", "0000:83:00.0"},
			expResults: NvmeControllerResults{
				{
					Pciaddr: "0000:81:00.0",
					State:   new(ResponseState),
				},
				{
					Pciaddr: "0000:83:00.0",
					State: &ResponseState{
						Status: ResponseStatus_CTL_ERR_NVME,
						Error:  "0000:83:00.0: " + msgBdevNotFound,
					},
				},
			},
			expCtrlrs: newDefaultCtrlrs(),
			desc:      "first device found, second not discovered",
		},
		{
			formatted:    false,
			devFormatRet: nil,
			pciAddrs:     []string{"0000:83:00.0", "0000:81:00.0"},
			expResults: NvmeControllerResults{
				{
					Pciaddr: "0000:83:00.0",
					State: &ResponseState{
						Status: ResponseStatus_CTL_ERR_NVME,
						Error:  "0000:83:00.0: " + msgBdevNotFound,
					},
				},
				{
					Pciaddr: "0000:81:00.0",
					State:   new(ResponseState),
				},
			},
			expCtrlrs: newDefaultCtrlrs(),
			desc:      "first not discovered, second found",
		},
		{
			formatted:    false,
			devFormatRet: errors.New("example format failure"),
			pciAddrs:     []string{"0000:83:00.0", "0000:81:00.0"},
			expResults: NvmeControllerResults{
				{
					Pciaddr: "0000:83:00.0",
					State: &ResponseState{
						Status: ResponseStatus_CTL_ERR_NVME,
						Error:  "0000:83:00.0: " + msgBdevNotFound,
					},
				},
				{
					Pciaddr: "0000:81:00.0",
					State: &ResponseState{
						Status: ResponseStatus_CTL_ERR_NVME,
						Error: "0000:81:00.0: " +
							"example format failure",
					},
				},
			},
			expCtrlrs: newDefaultCtrlrs(),
			desc:      "first not discovered, second failed to format",
		},
	}

	srvIdx := 0 // assume just a single io_server (index 0)

	for _, tt := range tests {
		t.Run(tt.desc, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			nvmeFormatCalls = []string{}

			config := defaultMockConfig(t)
			bdCfg := config.Servers[srvIdx].Storage.Bdev
			bdCfg.DeviceList = tt.pciAddrs

			c := MockController()
			// create nvmeStorage struct with customised test behaviour
			sn := newMockNvmeStorage(log, &mockExt{}, defaultMockSpdkEnv(),
				newMockSpdkNvme(log, []Controller{c}, []Namespace{},
					[]DeviceHealth{}, nil, tt.devFormatRet),
				false)
			sn.formatted = tt.formatted

			results := NvmeControllerResults{}

			if err := sn.Discover(); err != nil {
				t.Fatal(err)
			}

			sn.Format(bdCfg, &results)

			common.AssertEqual(
				t, len(results), len(tt.expResults),
				"unexpected number of response results, "+tt.desc)

			successPciaddrs := []string{}
			for i, result := range results {
				common.AssertEqual(
					t, result.State.Status, tt.expResults[i].State.Status,
					"unexpected response status, "+tt.desc)
				common.AssertEqual(
					t, result.State.Error, tt.expResults[i].State.Error,
					"unexpected result error message, "+tt.desc)
				common.AssertEqual(
					t, result.Pciaddr, tt.expResults[i].Pciaddr,
					"unexpected pciaddr, "+tt.desc)

				if result.State.Status == ResponseStatus_CTL_SUCCESS {
					if result.State.Info != msgBdevNoDevs {
						successPciaddrs = append(successPciaddrs, result.Pciaddr)
					}
				}
			}

			common.AssertEqual(
				t, nvmeFormatCalls, successPciaddrs,
				"unexpected list of pci addresses in format calls, "+tt.desc)
			common.AssertEqual(t, sn.formatted, true, "expect formatted state, "+tt.desc)

			common.AssertEqual(
				t, sn.controllers[0], tt.expCtrlrs[0],
				"unexpected list of discovered controllers, "+tt.desc)
		})
	}
}
