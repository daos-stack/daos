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

var nvmeFormatCalls []string

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

func (m *mockSpdkNvme) Discover() ([]Controller, []Namespace, error) {
	return m.initCtrlrs, m.initNss, m.discoverRet
}

func (m *mockSpdkNvme) Format(pciAddr string) ([]Controller, []Namespace, error) {
	if m.devFormatRet == nil {
		nvmeFormatCalls = append(nvmeFormatCalls, pciAddr)
	}

	return m.initCtrlrs, m.initNss, m.devFormatRet
}

func (m *mockSpdkNvme) Update(pciAddr string, path string, slot int32) (
	[]Controller, []Namespace, error) {
	c := MockController(m.fwRevAfter)

	return []Controller{c}, []Namespace{MockNamespace(&c)}, m.updateRet
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
		inited       bool
		formatted    bool
		devFormatRet error
		pciAddrs     []string
		expResults   []*pb.NvmeControllerResult
		desc         string
	}{
		{
			true,
			false,
			nil,
			[]string{},
			[]*pb.NvmeControllerResult{},
			"no devices",
		},
		{
			false,
			true,
			nil,
			[]string{},
			[]*pb.NvmeControllerResult{
				{
					Pciaddr: "",
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_APP,
						Error:  msgBdevNotInited,
					},
				},
			},
			"not initialized",
		},
		{
			true,
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
			true,
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
			true,
			false,
			nil,
			[]string{"0000:81:00.0"},
			[]*pb.NvmeControllerResult{
				{
					Pciaddr: "0000:81:00.0",
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_SUCCESS,
						Error:  "",
					},
				},
			},
			"single device",
		},
		{
			true,
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
			true,
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
			true,
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
			true,
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

		resp := new(pb.FormatStorageResp)

		if tt.inited {
			// not concerned with response
			sn.Discover(new(pb.ScanStorageResp))
		}

		sn.Format(srvIdx, resp)

		AssertEqual(
			t, len(resp.Crets), len(tt.expResults),
			"unexpected number of response results, "+tt.desc)

		successPciaddrs := []string{}
		for i, result := range resp.Crets {
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
				successPciaddrs = append(successPciaddrs, result.Pciaddr)
			}
		}

		AssertEqual(
			t, nvmeFormatCalls, successPciaddrs,
			"unexpected list of pci addresses in format calls, "+tt.desc)
		AssertEqual(t, sn.formatted, true, "expect formatted state, "+tt.desc)

		if tt.inited {
			AssertEqual(
				t, sn.controllers[0], pbC,
				"unexpected list of protobuf format controllers, "+tt.desc)
		}
	}
}

func TestUpdateNvmeStorage(t *testing.T) {
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

	// expected Controller protobuf representation should have updated
	// firmware revision
	c := MockControllerPB("1.0.1")
	srvIdx := 0

	for _, tt := range tests {
		config := defaultMockConfig(t)
		sn := defaultMockNvmeStorage(&config)

		if tt.inited {
			// not concerned with response
			sn.Discover(new(pb.ScanStorageResp))
		}

		if err := sn.Update(srvIdx, c.Pciaddr, "", 0); err != nil {
			if tt.errMsg != "" {
				ExpectError(t, err, tt.errMsg, "")
				continue
			}
			t.Fatal(err)
		}

		//fmt.Printf("%+v != %+v\n", sn.controllers[0], []*pb.NvmeController{c}[0])
		//		AssertEqual(
		//			t, sn.controllers, []*pb.NvmeController{c},
		//			"unexpected list of protobuf format controllers")
	}
}

// TestBurnInNvme verifies a corner case because BurnIn does not call out
// to SPDK via bindings.
// In this case the real NvmeStorage is used as opposed to a mockNvmeStorage.
func TestBurnInNvmeStorage(t *testing.T) {
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
