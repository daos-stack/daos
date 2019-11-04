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

// MockNamespace is a mock NVMe SSD namespace of type exported from go-spdk.
func MockNamespace() *Namespace {
	n := common.MockNamespacePB()
	return &Namespace{
		ID:   n.Id,
		Size: n.Capacity,
	}
}

// MockDeviceHealth is a mock NVMe SSD device health of type exported from go-spdk.
func MockDeviceHealth() *DeviceHealth {
	h := common.MockDeviceHealthPB()
	return &DeviceHealth{
		Temp:            h.Temp,
		TempWarnTime:    h.Tempwarn,
		TempCritTime:    h.Tempcrit,
		CtrlBusyTime:    h.Ctrlbusy,
		PowerCycles:     h.Powercycles,
		PowerOnHours:    h.Poweronhours,
		UnsafeShutdowns: h.Unsafeshutdowns,
		MediaErrors:     h.Mediaerrors,
		ErrorLogEntries: h.Errorlogs,
		TempWarn:        h.Tempwarning,
		AvailSpareWarn:  h.Availspare,
		ReliabilityWarn: h.Reliability,
		ReadOnlyWarn:    h.Readonly,
		VolatileWarn:    h.Volatilemem,
	}
}

// MockController is a mock NVMe SSD controller of type exported from go-spdk.
func MockController() Controller {
	c := common.MockControllerPB()
	return Controller{
		Model:      c.Model,
		Serial:     c.Serial,
		PCIAddr:    c.Pciaddr,
		FWRev:      c.Fwrev,
		SocketID:   c.Socketid,
		Health:     MockDeviceHealth(),
		Namespaces: []*Namespace{MockNamespace()},
	}
}

// NewMockController specifies customer details fr a mock NVMe SSD controller.
func NewMockController(pciaddr string, fwrev string, model string, serial string,
	socketID int32, health *DeviceHealth, namespaces []*Namespace) Controller {
	return Controller{
		Model:      model,
		Serial:     serial,
		PCIAddr:    pciaddr,
		FWRev:      fwrev,
		SocketID:   socketID,
		Health:     health,
		Namespaces: namespaces,
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
	discoverRet  error // NVME interface Discover() return value
	devFormatRet error // NVME interface Format() return value
}

// Discover mock implementation returns mock lists of devices
func (m *mockSpdkNvme) Discover() ([]Controller, error) {
	return m.initCtrlrs, m.discoverRet
}

// Format mock implementation records calls on devices with given pci address
func (m *mockSpdkNvme) Format(pciAddr string) ([]Controller, error) {
	if m.devFormatRet == nil {
		nvmeFormatCalls = append(nvmeFormatCalls, pciAddr)
	}

	return m.initCtrlrs, m.devFormatRet
}

func (m *mockSpdkNvme) Cleanup() {}

func newMockSpdkNvme(log logging.Logger, ctrlrs []Controller, discoverRet error, devFormatRet error) NVME {
	return &mockSpdkNvme{log, ctrlrs, discoverRet, devFormatRet}
}

func defaultMockSpdkNvme(log logging.Logger) NVME {
	return newMockSpdkNvme(log, []Controller{MockController()}, nil, nil)
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

func TestDiscoverNvme(t *testing.T) {
	c := MockController()

	for name, tc := range map[string]struct {
		inited          bool
		spdkInitEnvRet  error           // return value from go-spdk pkg
		spdkDiscoverRet error           // return value from go-spdk pkg
		inCtrlrs        []Controller    // discovered lib/spdk format
		outCtrlrs       NvmeControllers // protobuf format
		expErr          error
	}{
		"already initialised": {
			inited:    true,
			outCtrlrs: NvmeControllers{},
		},
		"single controller": {},
		"multiple controllers": {
			inCtrlrs:  []Controller{MockController(), MockController()},
			outCtrlrs: NvmeControllers{common.MockControllerPB(), common.MockControllerPB()},
		},
		"multiple namespaces": {
			inCtrlrs: []Controller{
				NewMockController(c.PCIAddr, c.Model, c.Serial, c.FWRev, c.SocketID,
					MockDeviceHealth(), []*Namespace{MockNamespace(), MockNamespace()}),
			},
			outCtrlrs: NvmeControllers{
				common.NewMockControllerPB(c.PCIAddr, c.Model, c.Serial, c.FWRev, c.SocketID,
					NvmeNamespaces{common.MockNamespacePB(), common.MockNamespacePB()},
					common.MockDeviceHealthPB()),
			},
		},
		"spdk discovery failure": {
			spdkDiscoverRet: errors.New("spdk example failure"),
			expErr:          errors.New(msgSpdkDiscoverFail + ": spdk example failure"),
		},
		"spdk init failure": {
			spdkInitEnvRet: errors.New("spdk example failure"),
			expErr:         errors.New(msgSpdkInitFail + ": spdk example failure"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.inCtrlrs == nil {
				tc.inCtrlrs = []Controller{MockController()}
			}
			if tc.outCtrlrs == nil {
				tc.outCtrlrs = NvmeControllers{common.MockControllerPB()}
			}

			sn := newMockNvmeStorage(log, &mockExt{}, newMockSpdkEnv(tc.spdkInitEnvRet),
				newMockSpdkNvme(log, tc.inCtrlrs, tc.spdkDiscoverRet, nil),
				tc.inited)

			if err := sn.Discover(); err != nil {
				if tc.expErr != nil {
					common.CmpErr(t, err, tc.expErr)
					return
				}
				t.Fatal(err)
			}

			if diff := cmp.Diff(tc.outCtrlrs.String(), sn.controllers.String()); diff != "" {
				t.Fatalf("unexpected controller results (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestFormatNvme(t *testing.T) {
	for name, tc := range map[string]struct {
		formatted    bool
		devFormatRet error
		pciAddrs     []string
		expResults   NvmeControllerResults
		expCtrlrs    NvmeControllers
	}{
		"no devices": {
			expResults: NvmeControllerResults{
				{
					Pciaddr: "",
					State: &ResponseState{
						Status: ResponseStatus_CTL_SUCCESS,
						Info:   msgBdevNoDevs,
					},
				},
			},
		},
		"already formatted": {
			formatted: true,
			expResults: NvmeControllerResults{
				{
					Pciaddr: "",
					State: &ResponseState{
						Status: ResponseStatus_CTL_ERR_APP,
						Error:  msgBdevAlreadyFormatted,
					},
				},
			},
		},
		"empty device string": {
			pciAddrs: []string{""},
			expResults: NvmeControllerResults{
				{
					Pciaddr: "",
					State: &ResponseState{
						Status: ResponseStatus_CTL_ERR_CONF,
						Error:  msgBdevEmpty,
					},
				},
			},
		},
		"single device": {
			pciAddrs: []string{"0000:81:00.0"},
			expResults: NvmeControllerResults{
				{
					Pciaddr: "0000:81:00.0",
					State:   new(ResponseState),
				},
			},
		},
		"single device not discovered": {
			pciAddrs: []string{"0000:83:00.0"},
			expResults: NvmeControllerResults{
				{
					Pciaddr: "0000:83:00.0",
					State: &ResponseState{
						Status: ResponseStatus_CTL_ERR_NVME,
						Error:  "0000:83:00.0: " + msgBdevNotFound,
					},
				},
			},
		},
		"first device found, second not discovered": {
			pciAddrs: []string{"0000:81:00.0", "0000:83:00.0"},
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
		},
		"first not discovered, second found": {
			pciAddrs: []string{"0000:83:00.0", "0000:81:00.0"},
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
		},
		"first not discovered, second failed to format": {
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
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			nvmeFormatCalls = []string{}

			config := defaultMockConfig(t)
			bdCfg := config.Servers[0].Storage.Bdev
			bdCfg.DeviceList = tc.pciAddrs

			c := MockController()

			if tc.expCtrlrs == nil {
				tc.expCtrlrs = NvmeControllers{common.MockControllerPB()}
			}

			sn := newMockNvmeStorage(log, &mockExt{}, defaultMockSpdkEnv(),
				newMockSpdkNvme(log, []Controller{c}, nil, tc.devFormatRet),
				false)
			sn.formatted = tc.formatted

			results := NvmeControllerResults{}

			if err := sn.Discover(); err != nil {
				t.Fatal(err)
			}

			sn.Format(bdCfg, &results)

			if diff := cmp.Diff(tc.expResults, results); diff != "" {
				t.Fatalf("unexpected controller results (-want, +got):\n%s\n", diff)
			}

			successPciaddrs := []string{}
			for _, result := range results {
				if result.State.Status == ResponseStatus_CTL_SUCCESS {
					if result.State.Info != msgBdevNoDevs {
						successPciaddrs = append(successPciaddrs, result.Pciaddr)
					}
				}
			}
			common.AssertEqual(t, nvmeFormatCalls, successPciaddrs,
				"unexpected list of pci addresses in format calls")
			common.AssertEqual(t, sn.formatted, true, "expect formatted state")

			if diff := cmp.Diff(tc.expCtrlrs, sn.controllers); diff != "" {
				t.Fatalf("unexpected controller results (-want, +got):\n%s\n", diff)
			}
		})
	}
}
