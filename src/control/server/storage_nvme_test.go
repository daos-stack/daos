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

package main

import (
	"fmt"
	"strings"
	"testing"

	. "github.com/daos-stack/daos/src/control/common"
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	. "github.com/daos-stack/go-spdk/spdk"
	"github.com/pkg/errors"
)

var nvmeFormatCalls []string // record calls to nvme.Format()

// MockController is a mock NVMe SSD controller of type exported from go-spdk.
func MockController(fwrev string) Controller {
	c := MockControllerPB(fwrev)
	return Controller{
		Model:   c.Model,
		Serial:  c.Serial,
		PCIAddr: c.Pciaddr,
		FWRev:   fwrev,
	}
}

// NewMockController specifies customer details fr a mock NVMe SSD controller.
func NewMockController(
	pciaddr string, fwrev string, model string, serial string) Controller {

	return Controller{
		Model:   model,
		Serial:  serial,
		PCIAddr: pciaddr,
		FWRev:   fwrev,
	}
}

// MockNamespace is a mock NVMe SSD namespace of type exported from go-spdk.
func MockNamespace(ctrlr *Controller) Namespace {
	n := MockNamespacePB()
	return Namespace{
		ID:           n.Id,
		Size:         n.Capacity,
		CtrlrPciAddr: ctrlr.PCIAddr,
	}
}

// mock external interface implementations for go-spdk/spdk package
type mockSpdkEnv struct {
	initRet error // ENV interface InitSPDKEnv() return value
}

func (m *mockSpdkEnv) InitSPDKEnv(int) error { return m.initRet }

func newMockSpdkEnv(initRet error) ENV { return &mockSpdkEnv{initRet} }

func defaultMockSpdkEnv() ENV { return newMockSpdkEnv(nil) }

// mock external interface implementations for go-spdk/nvme package
type mockSpdkNvme struct {
	fwRevBefore  string
	fwRevAfter   string
	initCtrlrs   []Controller
	initNss      []Namespace
	discoverRet  error // NVME interface Discover() return value
	devFormatRet error // NVME interface Format() return value
	updateRet    error // NVME interface Update() return value
}

// Discover mock implementation returns mock lists of devices
func (m *mockSpdkNvme) Discover() ([]Controller, []Namespace, error) {
	return m.initCtrlrs, m.initNss, m.discoverRet
}

// Format mock implementation records calls on devices with given pci address
func (m *mockSpdkNvme) Format(pciAddr string) ([]Controller, []Namespace, error) {
	if m.devFormatRet == nil {
		nvmeFormatCalls = append(nvmeFormatCalls, pciAddr)
	}

	return m.initCtrlrs, m.initNss, m.devFormatRet
}

// Update mock implementation modifies Fwrev of device with given pci address
func (m *mockSpdkNvme) Update(pciAddr string, path string, slot int32) (
	[]Controller, []Namespace, error) {

	for i, ctrlr := range m.initCtrlrs {
		if ctrlr.PCIAddr == pciAddr && m.updateRet == nil {
			m.initCtrlrs[i].FWRev = m.fwRevAfter
		}
	}

	return m.initCtrlrs, m.initNss, m.updateRet
}

func (m *mockSpdkNvme) Cleanup() { return }

func newMockSpdkNvme(
	fwBefore string, fwAfter string, ctrlrs []Controller, nss []Namespace,
	discoverRet error, devFormatRet error, updateRet error) NVME {

	return &mockSpdkNvme{
		fwBefore, fwAfter, ctrlrs, nss,
		discoverRet, devFormatRet, updateRet,
	}
}

func defaultMockSpdkNvme() NVME {
	c := MockController("1.0.0")

	return newMockSpdkNvme(
		"1.0.0", "1.0.1",
		[]Controller{c}, []Namespace{MockNamespace(&c)},
		nil, nil, nil)
}

// mock external interface implementations for spdk setup script
// TODO: provide capability to return values from mock
type mockSpdkSetup struct{}

func (m *mockSpdkSetup) prep(int, string, string) error { return nil }
func (m *mockSpdkSetup) reset() error                   { return nil }

// mockNvmeStorage factory
func newMockNvmeStorage(
	spdkEnv ENV, spdkNvme NVME, inited bool,
	config *configuration) *nvmeStorage {

	return &nvmeStorage{
		env:         spdkEnv,
		nvme:        spdkNvme,
		spdk:        &mockSpdkSetup{},
		config:      config,
		initialized: inited,
	}
}

// defaultMockNvmeStorage factory
func defaultMockNvmeStorage(config *configuration) *nvmeStorage {
	return newMockNvmeStorage(
		defaultMockSpdkEnv(),
		defaultMockSpdkNvme(),
		false, // Discover will not fetch when initialised is true
		config)
}

func TestDiscoverNvmeSingle(t *testing.T) {
	tests := []struct {
		inited          bool
		spdkInitEnvRet  error // return value from go-spdk pkg
		spdkDiscoverRet error // return value from go-spdk pkg
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
	}

	c := MockController("1.0.0")
	pbC := MockControllerPB("1.0.0")

	for _, tt := range tests {
		config := defaultMockConfig(t)
		sn := newMockNvmeStorage(
			newMockSpdkEnv(tt.spdkInitEnvRet),
			newMockSpdkNvme(
				"1.0.0", "1.0.1",
				[]Controller{c}, []Namespace{MockNamespace(&c)},
				tt.spdkDiscoverRet, nil, nil),
			tt.inited,
			&config)

		resp := new(pb.ScanStorageResp)
		sn.Discover(resp)
		if tt.errMsg != "" {
			AssertEqual(t, resp.Nvmestate.Error, tt.errMsg, "")
			AssertTrue(
				t,
				resp.Nvmestate.Status != pb.ResponseStatus_CTRL_SUCCESS,
				"")
			continue
		}
		AssertEqual(t, resp.Nvmestate.Error, "", "")
		AssertEqual(t, resp.Nvmestate.Status, pb.ResponseStatus_CTRL_SUCCESS, "")

		if tt.inited {
			AssertEqual(
				t, sn.controllers, []*pb.NvmeController(nil),
				"unexpected list of protobuf format controllers")
			continue
		}

		AssertEqual(
			t, sn.controllers, []*pb.NvmeController{pbC},
			"unexpected list of protobuf format controllers")
	}
}

// Verify correct mapping of namespaces to multiple controllers
func TestDiscoverNvmeMulti(t *testing.T) {
	tests := []struct {
		ctrlrs []Controller
		nss    []Namespace
	}{
		{
			[]Controller{
				{"", "", "1.2.3.4.5", "1.0.0"},
				{"", "", "1.2.3.4.6", "1.0.0"},
			},
			[]Namespace{
				{0, 100, "1.2.3.4.5"},
				{1, 200, "1.2.3.4.6"},
			},
		},
		{
			[]Controller{
				{"", "", "1.2.3.4.5", "1.0.0"},
				{"", "", "1.2.3.4.6", "1.0.0"},
			},
			[]Namespace{},
		},
		{
			[]Controller{
				{"", "", "1.2.3.4.5", "1.0.0"},
				{"", "", "1.2.3.4.6", "1.0.0"},
			},
			[]Namespace{
				{0, 100, "1.2.3.4.5"},
				{1, 100, "1.2.3.4.5"},
				{2, 100, "1.2.3.4.5"},
				{0, 200, "1.2.3.4.6"},
				{1, 200, "1.2.3.4.6"},
				{2, 200, "1.2.3.4.6"},
			},
		},
	}

	for _, tt := range tests {
		config := defaultMockConfig(t)
		sn := newMockNvmeStorage(
			defaultMockSpdkEnv(),
			newMockSpdkNvme(
				"1.0.0", "1.0.1", tt.ctrlrs, tt.nss,
				nil, nil, nil),
			false,
			&config)

		// not concerned with response
		sn.Discover(new(pb.ScanStorageResp))

		if len(tt.ctrlrs) != len(sn.controllers) {
			t.Fatalf(
				"unexpected number of controllers found, wanted %d, found %d",
				len(tt.ctrlrs), len(sn.controllers))
		}

		// verify we have the expected number of namespaces reported
		discovered := 0
		for _, pbC := range sn.controllers {
			discovered += len(pbC.Namespaces)
		}
		if len(tt.nss) != discovered {
			t.Fatalf(
				"unexpected number of namespaces found, wanted %d, found %d",
				len(tt.nss), discovered)
		}

		// verify protobuf Controller has ns for each one expected
		for _, n := range tt.nss {
			foundNs := false // find namespace
			for i, pbC := range sn.controllers {
				if n.CtrlrPciAddr == pbC.Pciaddr {
					for _, pbNs := range sn.controllers[i].Namespaces {
						if pbNs.Capacity == n.Size && pbNs.Id == n.ID {
							foundNs = true
						}
					}
				}
			}
			if !foundNs {
				t.Fatalf("namespace not found: %v", n)
			}
		}
	}
}

func TestFormatNvme(t *testing.T) {
	tests := []struct {
		formatted    bool
		devFormatRet error
		pciAddrs     []string
		expResults   []*pb.NvmeControllerResult
		desc         string
	}{
		{
			false,
			nil,
			[]string{},
			[]*pb.NvmeControllerResult{
				{
					Pciaddr: "",
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_SUCCESS,
						Info:   msgBdevNoDevs,
					},
				},
			},
			"no devices",
		},
		{
			true,
			nil,
			[]string{},
			[]*pb.NvmeControllerResult{
				{
					Pciaddr: "",
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_APP,
						Error:  msgBdevAlreadyFormatted,
					},
				},
			},
			"already formatted",
		},
		{
			false,
			nil,
			[]string{""},
			[]*pb.NvmeControllerResult{
				{
					Pciaddr: "",
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_CONF,
						Error:  msgBdevEmpty,
					},
				},
			},
			"empty device string",
		},
		{
			false,
			nil,
			[]string{"0000:81:00.0"},
			[]*pb.NvmeControllerResult{
				{
					Pciaddr: "0000:81:00.0",
					State:   new(pb.ResponseState),
				},
			},
			"single device",
		},
		{
			false,
			nil,
			[]string{"0000:83:00.0"},
			[]*pb.NvmeControllerResult{
				{
					Pciaddr: "0000:83:00.0",
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_NVME,
						Error:  "0000:83:00.0: " + msgBdevNotFound,
					},
				},
			},
			"single device not discovered",
		},
		{
			false,
			nil,
			[]string{"0000:81:00.0", "0000:83:00.0"},
			[]*pb.NvmeControllerResult{
				{
					Pciaddr: "0000:81:00.0",
					State:   new(pb.ResponseState),
				},
				{
					Pciaddr: "0000:83:00.0",
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_NVME,
						Error:  "0000:83:00.0: " + msgBdevNotFound,
					},
				},
			},
			"first device found, second not discovered",
		},
		{
			false,
			nil,
			[]string{"0000:83:00.0", "0000:81:00.0"},
			[]*pb.NvmeControllerResult{
				{
					Pciaddr: "0000:83:00.0",
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_NVME,
						Error:  "0000:83:00.0: " + msgBdevNotFound,
					},
				},
				{
					Pciaddr: "0000:81:00.0",
					State:   new(pb.ResponseState),
				},
			},
			"first not discovered, second found",
		},
		{
			false,
			errors.New("example format failure"),
			[]string{"0000:83:00.0", "0000:81:00.0"},
			[]*pb.NvmeControllerResult{
				{
					Pciaddr: "0000:83:00.0",
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_NVME,
						Error:  "0000:83:00.0: " + msgBdevNotFound,
					},
				},
				{
					Pciaddr: "0000:81:00.0",
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_NVME,
						Error: "0000:81:00.0: " +
							"example format failure",
					},
				},
			},
			"first not discovered, second failed to format",
		},
	}

	pbC := MockControllerPB("1.0.0")
	srvIdx := 0 // assume just a single io_server (index 0)

	for _, tt := range tests {
		nvmeFormatCalls = []string{}

		config := defaultMockConfig(t)
		config.Servers[srvIdx].BdevList = tt.pciAddrs

		c := MockController("1.0.0")
		// create nvmeStorage struct with customised test behaviour
		sn := newMockNvmeStorage(
			defaultMockSpdkEnv(),
			newMockSpdkNvme(
				"1.0.0", "1.0.1",
				[]Controller{c}, []Namespace{MockNamespace(&c)},
				nil, tt.devFormatRet, nil),
			false, &config)
		sn.formatted = tt.formatted

		results := []*pb.NvmeControllerResult{}

		// not concerned with response
		sn.Discover(new(pb.ScanStorageResp))

		sn.Format(srvIdx, &results)

		AssertEqual(
			t, len(results), len(tt.expResults),
			"unexpected number of response results, "+tt.desc)

		successPciaddrs := []string{}
		for i, result := range results {
			AssertEqual(
				t, result.State.Status, tt.expResults[i].State.Status,
				"unexpected response status, "+tt.desc)
			AssertEqual(
				t, result.State.Error, tt.expResults[i].State.Error,
				"unexpected result error message, "+tt.desc)
			AssertEqual(
				t, result.Pciaddr, tt.expResults[i].Pciaddr,
				"unexpected pciaddr, "+tt.desc)

			if result.State.Status == pb.ResponseStatus_CTRL_SUCCESS {
				if result.State.Info != msgBdevNoDevs {
					successPciaddrs = append(successPciaddrs, result.Pciaddr)
				}
			}
		}

		AssertEqual(
			t, nvmeFormatCalls, successPciaddrs,
			"unexpected list of pci addresses in format calls, "+tt.desc)
		AssertEqual(t, sn.formatted, true, "expect formatted state, "+tt.desc)

		AssertEqual(
			t, sn.controllers[0], pbC,
			"unexpected list of discovered controllers, "+tt.desc)
	}
}

func TestUpdateNvme(t *testing.T) {
	pciAddr := "0000:81:00.0" // default pciaddr for tests
	model := "ABC"            // only update if ctrlr model name matches
	serial := "123ABC"
	startRev := "1.0.0"      // only update if at specified starting revision
	defaultEndRev := "1.0.1" // default fw revision after update
	newDefaultCtrlrs := func(rev string) []*pb.NvmeController {
		return []*pb.NvmeController{
			NewMockControllerPB(
				pciAddr, rev, model, serial,
				[]*pb.NvmeController_Namespace(nil)),
		}
	}

	tests := []struct {
		inited       bool
		devUpdateRet error
		pciAddrs     []string                   // pci addresses in config to be updated
		endRev       *string                    // force resultant revision to test against
		initCtrlrs   []Controller               // initially discovered ctrlrs
		expResults   []*pb.NvmeControllerResult // expected response results
		expCtrlrs    []*pb.NvmeController       // expected resultant ctrlr details
		desc         string
	}{
		{
			inited: true,
			desc:   "no devices",
		},
		{
			inited:   true,
			pciAddrs: []string{""},
			expResults: []*pb.NvmeControllerResult{
				{
					Pciaddr: "",
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_CONF,
						Error:  msgBdevEmpty,
					},
				},
			},
			desc: "empty device string",
		},
		{
			inited:     true,
			pciAddrs:   []string{pciAddr},
			initCtrlrs: []Controller{MockController(startRev)},
			expResults: []*pb.NvmeControllerResult{
				{
					Pciaddr: pciAddr,
					State:   new(pb.ResponseState),
				},
			},
			expCtrlrs: newDefaultCtrlrs(defaultEndRev),
			desc:      "single device successfully discovered",
		},
		{
			inited:     true,
			pciAddrs:   []string{"0000:aa:00.0"},
			initCtrlrs: []Controller{MockController(startRev)},
			expResults: []*pb.NvmeControllerResult{
				{
					Pciaddr: "0000:aa:00.0",
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_NVME,
						Error:  "0000:aa:00.0: " + msgBdevNotFound,
					},
				},
			},
			expCtrlrs: newDefaultCtrlrs(startRev),
			desc:      "single device not discovered",
		},
		{
			inited:   true,
			pciAddrs: []string{pciAddr},
			initCtrlrs: []Controller{ // device has different model
				NewMockController(pciAddr, startRev, "UKNOWN1", serial),
			},
			expResults: []*pb.NvmeControllerResult{
				{
					Pciaddr: pciAddr,
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_NVME,
						Error: pciAddr + ": " +
							msgBdevModelMismatch +
							" want " + model + ", have UKNOWN1",
					},
				},
			},
			expCtrlrs: []*pb.NvmeController{
				NewMockControllerPB(
					pciAddr, startRev, "UKNOWN1", serial,
					[]*pb.NvmeController_Namespace(nil)),
			},
			desc: "single device different model",
		},
		{
			inited:   true,
			pciAddrs: []string{pciAddr},
			initCtrlrs: []Controller{ // device has different start rev
				NewMockController(pciAddr, "2.0.0", model, serial),
			},
			expResults: []*pb.NvmeControllerResult{
				{
					Pciaddr: pciAddr,
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_NVME,
						Error: pciAddr + ": " +
							msgBdevFwrevStartMismatch +
							" want 1.0.0, have 2.0.0",
					},
				},
			},
			expCtrlrs: []*pb.NvmeController{
				NewMockControllerPB(
					pciAddr, "2.0.0", model, serial,
					[]*pb.NvmeController_Namespace(nil)),
			},
			desc: "single device different starting rev",
		},
		{
			inited:       true,
			pciAddrs:     []string{pciAddr},
			devUpdateRet: errors.New("spdk format failed"),
			initCtrlrs:   []Controller{MockController(startRev)},
			expResults: []*pb.NvmeControllerResult{
				{
					Pciaddr: pciAddr,
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_NVME,
						Error: pciAddr + ": " +
							"*main.mockSpdkNvme: " +
							"spdk format failed",
					},
				},
			},
			expCtrlrs: newDefaultCtrlrs(startRev),
			desc:      "single device update fails",
		},
		{
			inited:     true,
			pciAddrs:   []string{pciAddr},
			endRev:     &startRev, // force resultant rev, non-update
			initCtrlrs: []Controller{MockController(startRev)},
			expResults: []*pb.NvmeControllerResult{
				{
					Pciaddr: pciAddr,
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_NVME,
						Error: pciAddr + ": " +
							msgBdevFwrevEndMismatch,
					},
				},
			},
			expCtrlrs: newDefaultCtrlrs(startRev), // match forced endRev
			desc:      "single device same rev after update",
		},
		{
			inited:     true,
			pciAddrs:   []string{pciAddr},
			endRev:     new(string), // force resultant rev, non-update
			initCtrlrs: []Controller{MockController(startRev)},
			expResults: []*pb.NvmeControllerResult{
				{
					Pciaddr: pciAddr,
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_NVME,
						Error: pciAddr + ": " +
							msgBdevFwrevEndMismatch,
					},
				},
			},
			expCtrlrs: newDefaultCtrlrs(""), // match forced endRev
			desc:      "single device empty rev after update",
		},
		{
			inited: true,
			pciAddrs: []string{
				pciAddr, "0000:81:00.1", "0000:aa:00.0", "0000:ab:00.0",
			},
			initCtrlrs: []Controller{
				NewMockController("0000:ab:00.0", startRev, "UKN", serial),
				NewMockController("0000:aa:00.0", defaultEndRev, model, serial),
				NewMockController("0000:81:00.1", startRev, model, serial),
			},
			expResults: []*pb.NvmeControllerResult{
				{
					Pciaddr: pciAddr,
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_NVME,
						Error:  pciAddr + ": " + msgBdevNotFound,
					},
				},
				{
					Pciaddr: "0000:81:00.1",
					State:   new(pb.ResponseState),
				},
				{
					Pciaddr: "0000:aa:00.0",
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_NVME,
						Error: "0000:aa:00.0: " +
							msgBdevFwrevStartMismatch +
							" want 1.0.0, have 1.0.1",
					},
				},
				{
					Pciaddr: "0000:ab:00.0",
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_NVME,
						Error: "0000:ab:00.0: " +
							msgBdevModelMismatch +
							" want ABC, have UKN",
					},
				},
			},
			expCtrlrs: []*pb.NvmeController{
				NewMockControllerPB(
					"0000:ab:00.0", startRev, "UKN", serial,
					[]*pb.NvmeController_Namespace(nil)),
				NewMockControllerPB(
					"0000:aa:00.0", defaultEndRev, model, serial,
					[]*pb.NvmeController_Namespace(nil)),
				NewMockControllerPB(
					"0000:81:00.1", defaultEndRev, model, serial,
					[]*pb.NvmeController_Namespace(nil)),
			},
			desc: "multiple devices (missing,mismatch rev/model,success)",
		},
	}

	srvIdx := 0 // assume just a single io_server (index 0)

	for _, tt := range tests {
		config := defaultMockConfig(t)
		config.Servers[srvIdx].BdevList = tt.pciAddrs
		endRev := defaultEndRev
		if tt.endRev != nil { // non default endRev specified
			endRev = *tt.endRev
		}

		// create nvmeStorage struct with customised test behaviour
		sn := newMockNvmeStorage(
			defaultMockSpdkEnv(),
			newMockSpdkNvme( // mock nvme subsystem
				startRev, endRev, // ctrlr before/after fw revs
				tt.initCtrlrs, []Namespace{}, // Nss ignored
				nil, nil, tt.devUpdateRet),
			false, &config)

		results := []*pb.NvmeControllerResult{}

		if tt.inited {
			sn.Discover(new(pb.ScanStorageResp)) // not concerned with response
		}

		// create parameters message with desired model name & starting fwrev
		req := &pb.UpdateNvmeReq{
			Startrev: startRev, Model: model, Path: "", Slot: 0,
		}
		// call with io_server index, req and results list to populate
		sn.Update(srvIdx, req, &results)

		// verify expected response results have been populated
		AssertEqual(
			t, len(results), len(tt.expResults),
			"unexpected number of response results, "+tt.desc)

		successPciaddrs := []string{}
		for i, result := range results {
			AssertEqual(
				t, result.State.Error, tt.expResults[i].State.Error,
				"unexpected result error message, "+tt.desc)
			AssertEqual(
				t, result.State.Status, tt.expResults[i].State.Status,
				"unexpected response status, "+tt.desc)
			AssertEqual(
				t, result.Pciaddr, tt.expResults[i].Pciaddr,
				"unexpected pciaddr, "+tt.desc)

			if result.State.Status == pb.ResponseStatus_CTRL_SUCCESS {
				successPciaddrs = append(successPciaddrs, result.Pciaddr)
			}
		}

		// verify controller details have been updated
		AssertEqual(
			t, len(sn.controllers), len(tt.expCtrlrs),
			"unexpected number of controllers, "+tt.desc)

		for i, c := range sn.controllers {
			AssertEqual(
				t, c, tt.expCtrlrs[i],
				fmt.Sprintf(
					"entry %d in list of discovered controllers, %s\n",
					i, tt.desc))
		}
	}
}

// TestBurnInNvme verifies a corner case because BurnIn does not call out
// to SPDK via bindings.
// In this case the real NvmeStorage is used as opposed to a mockNvmeStorage.
func TestBurnInNvme(t *testing.T) {
	tests := []struct {
		inited bool
		errMsg string
	}{
		{
			true,
			"",
		},
		{
			false,
			"nvme storage not initialized",
		},
	}

	c := MockControllerPB("1.0.0")
	configPath := "/foo/bar/conf.fio"
	nsID := 1
	expectedArgs := []string{
		fmt.Sprintf(
			"--filename=\"trtype=PCIe traddr=%s ns=%d\"",
			strings.Replace(c.Pciaddr, ":", ".", -1), nsID),
		"--ioengine=spdk",
		"--eta=always",
		"--eta-newline=10",
		configPath,
	}

	for _, tt := range tests {
		config := defaultMockConfig(t)
		sn := defaultMockNvmeStorage(&config)

		if tt.inited {
			// not concerned with response
			sn.Discover(new(pb.ScanStorageResp))
		}

		cmdName, args, env, err := sn.BurnIn(c.Pciaddr, int32(nsID), configPath)
		if err != nil {
			if tt.errMsg != "" {
				ExpectError(t, err, tt.errMsg, "")
				continue
			}
			t.Fatal(err)
		}

		AssertTrue(t, strings.HasSuffix(cmdName, "bin/fio"), "unexpected fio executable path")
		AssertEqual(t, args, expectedArgs, "unexpected list of command arguments")
		AssertTrue(t, strings.HasPrefix(env, "LD_PRELOAD="), "unexpected LD_PRELOAD fio_plugin executable path")
		AssertTrue(t, strings.HasSuffix(env, "spdk/fio_plugin/fio_plugin"), "unexpected LD_PRELOAD fio_plugin executable path")
	}
}
