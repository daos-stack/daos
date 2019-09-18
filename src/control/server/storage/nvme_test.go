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

package storage

import (
	"fmt"
	"strings"
	"testing"

	"github.com/pkg/errors"

	. "github.com/daos-stack/daos/src/control/common"
	. "github.com/daos-stack/daos/src/control/common/proto/ctl"
	. "github.com/daos-stack/daos/src/control/common/storage"
	. "github.com/daos-stack/daos/src/control/lib/spdk"
	"github.com/daos-stack/daos/src/control/logging"
	. "github.com/daos-stack/daos/src/control/server/storage/config"
	. "github.com/daos-stack/daos/src/control/server/storage/messages"
)

type mockNvmeExt struct{}

func (e *mockNvmeExt) getAbsInstallPath(path string) (string, error) {
	return path, nil
}

func testBdevConfig() BdevConfig {
	return BdevConfig{
		Class: BdevClassNvme,
	}
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
			errMsg:          MsgSpdkDiscoverFail + ": spdk example failure",
		},
		{
			spdkInitEnvRet: errors.New("spdk example failure"),
			errMsg:         MsgSpdkInitFail + ": spdk example failure",
		},
		{
			numa: 1,
		},
	}

	c := MockController("1.0.0")
	pbC := MockControllerPB("1.0.0")

	for _, tt := range tests {
		log, buf := logging.NewTestLogger(t.Name())
		defer ShowBufferOnFailure(t, buf)()

		c.SocketID = tt.numa
		pbC.Socketid = tt.numa

		sn := NewMockNvmeStorage(
			log,
			NewMockSpdkEnv(tt.spdkInitEnvRet),
			NewMockSpdkNvme(
				log,
				"1.0.0", "1.0.1",
				[]Controller{c}, []Namespace{MockNamespace(&c)},
				[]DeviceHealth{MockDeviceHealth(&c)},
				tt.spdkDiscoverRet, nil, nil),
			tt.inited)

		if err := sn.Discover(); err != nil {
			if tt.errMsg != "" {
				AssertEqual(t, err.Error(), tt.errMsg, "")
				continue
			}
			t.Fatal(err)
		}

		if tt.inited {
			AssertEqual(t, sn.Controllers(), NvmeControllers(nil),
				"unexpected list of protobuf format controllers")
			continue
		}

		AssertEqual(t, sn.Controllers(), NvmeControllers{pbC},
			"unexpected list of protobuf format controllers")
	}
}

// Verify correct mapping of namespaces to multiple controllers
func TestDiscoverNvmeMulti(t *testing.T) {
	tests := []struct {
		ctrlrs []Controller
		nss    []Namespace
		dh     []DeviceHealth
	}{
		{
			[]Controller{
				{"", "", "1.2.3.4.5", "1.0.0", 0},
				{"", "", "1.2.3.4.6", "1.0.0", 0},
			},
			[]Namespace{
				{0, 100, "1.2.3.4.5"},
				{1, 200, "1.2.3.4.6"},
			},
			[]DeviceHealth{
				{300, 0, 0, 0, 0, 1000, 1, 0, 0,
					false, false, false, false, false},
				{300, 0, 0, 0, 0, 1000, 1, 0, 0,
					false, false, false, false, false},
			},
		},
		{
			[]Controller{
				{"", "", "1.2.3.4.5", "1.0.0", 0},
				{"", "", "1.2.3.4.6", "1.0.0", 0},
			},
			[]Namespace{},
			[]DeviceHealth{
				{300, 0, 0, 0, 0, 1000, 1, 0, 0,
					false, false, false, false, false},
				{300, 0, 0, 0, 0, 1000, 1, 0, 0,
					false, false, false, false, false},
			},
		},
		{
			[]Controller{
				{"", "", "1.2.3.4.5", "1.0.0", 0},
				{"", "", "1.2.3.4.6", "1.0.0", 0},
			},
			[]Namespace{
				{0, 100, "1.2.3.4.5"},
				{1, 100, "1.2.3.4.5"},
				{2, 100, "1.2.3.4.5"},
				{0, 200, "1.2.3.4.6"},
				{1, 200, "1.2.3.4.6"},
				{2, 200, "1.2.3.4.6"},
			},
			[]DeviceHealth{
				{300, 0, 0, 0, 0, 1000, 1, 0, 0,
					false, false, false, false, false},
				{300, 0, 0, 0, 0, 1000, 1, 0, 0,
					false, false, false, false, false},
			},
		},
	}

	for _, tt := range tests {
		log, buf := logging.NewTestLogger(t.Name())
		defer ShowBufferOnFailure(t, buf)()

		sn := NewMockNvmeStorage(
			log,
			DefaultMockSpdkEnv(),
			NewMockSpdkNvme(
				log,
				"1.0.0", "1.0.1", tt.ctrlrs, tt.nss, tt.dh,
				nil, nil, nil),
			false)

		if err := sn.Discover(); err != nil {
			t.Fatal(err)
		}

		if len(tt.ctrlrs) != len(sn.Controllers()) {
			t.Fatalf(
				"unexpected number of controllers found, wanted %d, found %d",
				len(tt.ctrlrs), len(sn.Controllers()))
		}

		// verify we have the expected number of namespaces reported
		discovered := 0
		for _, pbC := range sn.Controllers() {
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
			for _, pbC := range sn.Controllers() {
				if n.CtrlrPciAddr == pbC.Pciaddr {
					for _, pbNs := range pbC.Namespaces {
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

		// verify we have expected number of health info per controller
		if len(tt.dh) != len(sn.Controllers()) {
			t.Fatalf(
				"unexpected number of device health info found, wanted %d, found %d",
				len(tt.dh), len(sn.Controllers()))
		}
	}
}

func TestFormatNvme(t *testing.T) {
	pciAddr := "0000:81:00.0"
	model := "ABC"
	serial := "123ABC"
	fwRev := "1.0.0"
	newDefaultCtrlrs := func() NvmeControllers {
		return NvmeControllers{
			NewMockControllerPB(
				pciAddr, fwRev, model, serial,
				NvmeNamespaces(nil),
				NvmeHealthstats(nil)),
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
						Status: ResponseStatus_CTRL_SUCCESS,
						Info:   MsgBdevNoDevs,
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
						Status: ResponseStatus_CTRL_ERR_APP,
						Error:  MsgBdevAlreadyFormatted,
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
						Status: ResponseStatus_CTRL_ERR_CONF,
						Error:  MsgBdevEmpty,
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
						Status: ResponseStatus_CTRL_ERR_NVME,
						Error:  "0000:83:00.0: " + MsgBdevNotFound,
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
						Status: ResponseStatus_CTRL_ERR_NVME,
						Error:  "0000:83:00.0: " + MsgBdevNotFound,
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
						Status: ResponseStatus_CTRL_ERR_NVME,
						Error:  "0000:83:00.0: " + MsgBdevNotFound,
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
						Status: ResponseStatus_CTRL_ERR_NVME,
						Error:  "0000:83:00.0: " + MsgBdevNotFound,
					},
				},
				{
					Pciaddr: "0000:81:00.0",
					State: &ResponseState{
						Status: ResponseStatus_CTRL_ERR_NVME,
						Error: "0000:81:00.0: " +
							"example format failure",
					},
				},
			},
			expCtrlrs: newDefaultCtrlrs(),
			desc:      "first not discovered, second failed to format",
		},
	}

	for _, tt := range tests {
		t.Run(tt.desc, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)()

			bdCfg := testBdevConfig()
			bdCfg.DeviceList = tt.pciAddrs

			c := MockController("1.0.0")
			mockNvme := NewMockSpdkNvme(
				log,
				"1.0.0", "1.0.1",
				[]Controller{c}, []Namespace{},
				[]DeviceHealth{},
				nil, tt.devFormatRet, nil)

			// create NvmeProvider struct with customised test behaviour
			sn := NewMockNvmeStorage(
				log,
				DefaultMockSpdkEnv(),
				mockNvme,
				false)
			sn.formatted = tt.formatted

			results := NvmeControllerResults{}

			if err := sn.Discover(); err != nil {
				t.Fatal(err)
			}

			sn.Format(bdCfg, &results)

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

				if result.State.Status == ResponseStatus_CTRL_SUCCESS {
					if result.State.Info != MsgBdevNoDevs {
						successPciaddrs = append(successPciaddrs, result.Pciaddr)
					}
				}
			}

			AssertEqual(
				t, mockNvme.(*mockSpdkNvme).formatCalls, successPciaddrs,
				"unexpected list of pci addresses in format calls, "+tt.desc)
			AssertEqual(t, sn.formatted, true, "expect formatted state, "+tt.desc)

			AssertEqual(
				t, sn.Controllers()[0], tt.expCtrlrs[0],
				"unexpected list of discovered controllers, "+tt.desc)
		})
	}
}

func TestUpdateNvme(t *testing.T) {
	pciAddr := "0000:81:00.0" // default pciaddr for tests
	model := "ABC"            // only update if ctrlr model name matches
	serial := "123ABC"
	startRev := "1.0.0"      // only update if at specified starting revision
	defaultEndRev := "1.0.1" // default fw revision after update
	newDefaultCtrlrs := func(rev string) NvmeControllers {
		return NvmeControllers{
			NewMockControllerPB(
				pciAddr, rev, model, serial,
				NvmeNamespaces(nil),
				NvmeHealthstats(nil)),
		}
	}

	tests := []struct {
		inited       bool
		devUpdateRet error
		pciAddrs     []string              // pci addresses in config to be updated
		endRev       *string               // force resultant revision to test against
		initCtrlrs   []Controller          // initially discovered ctrlrs
		expResults   NvmeControllerResults // expected response results
		expCtrlrs    NvmeControllers       // expected resultant ctrlr details
		desc         string
	}{
		{
			inited: true,
			desc:   "no devices",
		},
		{
			inited:   true,
			pciAddrs: []string{""},
			expResults: NvmeControllerResults{
				{
					Pciaddr: "",
					State: &ResponseState{
						Status: ResponseStatus_CTRL_ERR_CONF,
						Error:  MsgBdevEmpty,
					},
				},
			},
			desc: "empty device string",
		},
		{
			inited:     true,
			pciAddrs:   []string{pciAddr},
			initCtrlrs: []Controller{MockController(startRev)},
			expResults: NvmeControllerResults{
				{
					Pciaddr: pciAddr,
					State:   new(ResponseState),
				},
			},
			expCtrlrs: newDefaultCtrlrs(defaultEndRev),
			desc:      "single device successfully discovered",
		},
		{
			inited:     true,
			pciAddrs:   []string{"0000:aa:00.0"},
			initCtrlrs: []Controller{MockController(startRev)},
			expResults: NvmeControllerResults{
				{
					Pciaddr: "0000:aa:00.0",
					State: &ResponseState{
						Status: ResponseStatus_CTRL_ERR_NVME,
						Error:  "0000:aa:00.0: " + MsgBdevNotFound,
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
				NewMockController(pciAddr, startRev, "UKNOWN1", serial, 0),
			},
			expResults: NvmeControllerResults{
				{
					Pciaddr: pciAddr,
					State: &ResponseState{
						Status: ResponseStatus_CTRL_ERR_NVME,
						Error: pciAddr + ": " +
							MsgBdevModelMismatch +
							" want " + model + ", have UKNOWN1",
					},
				},
			},
			expCtrlrs: NvmeControllers{
				NewMockControllerPB(
					pciAddr, startRev, "UKNOWN1", serial,
					NvmeNamespaces(nil),
					NvmeHealthstats(nil)),
			},
			desc: "single device different model",
		},
		{
			inited:   true,
			pciAddrs: []string{pciAddr},
			initCtrlrs: []Controller{ // device has different start rev
				NewMockController(pciAddr, "2.0.0", model, serial, 0),
			},
			expResults: NvmeControllerResults{
				{
					Pciaddr: pciAddr,
					State: &ResponseState{
						Status: ResponseStatus_CTRL_ERR_NVME,
						Error: pciAddr + ": " +
							MsgBdevFwrevStartMismatch +
							" want 1.0.0, have 2.0.0",
					},
				},
			},
			expCtrlrs: NvmeControllers{
				NewMockControllerPB(
					pciAddr, "2.0.0", model, serial,
					NvmeNamespaces(nil),
					NvmeHealthstats(nil)),
			},
			desc: "single device different starting rev",
		},
		{
			inited:       true,
			pciAddrs:     []string{pciAddr},
			devUpdateRet: errors.New("spdk format failed"),
			initCtrlrs:   []Controller{MockController(startRev)},
			expResults: NvmeControllerResults{
				{
					Pciaddr: pciAddr,
					State: &ResponseState{
						Status: ResponseStatus_CTRL_ERR_NVME,
						Error: pciAddr + ": " +
							"*storage.mockSpdkNvme: " +
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
			expResults: NvmeControllerResults{
				{
					Pciaddr: pciAddr,
					State: &ResponseState{
						Status: ResponseStatus_CTRL_ERR_NVME,
						Error: pciAddr + ": " +
							MsgBdevFwrevEndMismatch,
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
			expResults: NvmeControllerResults{
				{
					Pciaddr: pciAddr,
					State: &ResponseState{
						Status: ResponseStatus_CTRL_ERR_NVME,
						Error: pciAddr + ": " +
							MsgBdevFwrevEndMismatch,
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
				NewMockController("0000:ab:00.0", startRev, "UKN", serial, 0),
				NewMockController("0000:aa:00.0", defaultEndRev, model, serial, 0),
				NewMockController("0000:81:00.1", startRev, model, serial, 0),
			},
			expResults: NvmeControllerResults{
				{
					Pciaddr: pciAddr,
					State: &ResponseState{
						Status: ResponseStatus_CTRL_ERR_NVME,
						Error:  pciAddr + ": " + MsgBdevNotFound,
					},
				},
				{
					Pciaddr: "0000:81:00.1",
					State:   new(ResponseState),
				},
				{
					Pciaddr: "0000:aa:00.0",
					State: &ResponseState{
						Status: ResponseStatus_CTRL_ERR_NVME,
						Error: "0000:aa:00.0: " +
							MsgBdevFwrevStartMismatch +
							" want 1.0.0, have 1.0.1",
					},
				},
				{
					Pciaddr: "0000:ab:00.0",
					State: &ResponseState{
						Status: ResponseStatus_CTRL_ERR_NVME,
						Error: "0000:ab:00.0: " +
							MsgBdevModelMismatch +
							" want ABC, have UKN",
					},
				},
			},
			expCtrlrs: NvmeControllers{
				NewMockControllerPB(
					"0000:ab:00.0", startRev, "UKN", serial,
					NvmeNamespaces(nil),
					NvmeHealthstats(nil)),
				NewMockControllerPB(
					"0000:aa:00.0", defaultEndRev, model, serial,
					NvmeNamespaces(nil),
					NvmeHealthstats(nil)),
				NewMockControllerPB(
					"0000:81:00.1", defaultEndRev, model, serial,
					NvmeNamespaces(nil),
					NvmeHealthstats(nil)),
			},
			desc: "multiple devices (missing,mismatch rev/model,success)",
		},
	}

	for _, tt := range tests {
		log, buf := logging.NewTestLogger(t.Name())
		defer ShowBufferOnFailure(t, buf)()

		bdCfg := testBdevConfig()
		bdCfg.DeviceList = tt.pciAddrs
		endRev := defaultEndRev
		if tt.endRev != nil { // non default endRev specified
			endRev = *tt.endRev
		}

		// create NvmeProvider struct with customised test behaviour
		sn := NewMockNvmeStorage(
			log,
			DefaultMockSpdkEnv(),
			NewMockSpdkNvme( // mock nvme subsystem
				log,
				startRev, endRev, // ctrlr before/after fw revs
				tt.initCtrlrs, []Namespace{}, // Nss ignored
				[]DeviceHealth{}, nil, nil, tt.devUpdateRet),
			false)

		results := NvmeControllerResults{}

		if tt.inited {
			if err := sn.Discover(); err != nil {
				t.Fatal(err)
			}
		}

		// create parameters message with desired model name & starting fwrev
		req := &UpdateNvmeReq{
			Startrev: startRev, Model: model, Path: "", Slot: 0,
		}
		// call with io_server index, req and results list to populate
		sn.Update(bdCfg, req, &results)

		// verify expected response results have been populated
		AssertEqual(
			t, len(results), len(tt.expResults),
			"unexpected number of response results, "+tt.desc)

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
		}

		// verify controller details have been updated
		AssertEqual(
			t, len(sn.Controllers()), len(tt.expCtrlrs),
			"unexpected number of controllers, "+tt.desc)

		for i, c := range sn.Controllers() {
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
		log, buf := logging.NewTestLogger(t.Name())
		defer ShowBufferOnFailure(t, buf)()

		sn := DefaultMockNvmeStorage(log)

		if tt.inited {
			if err := sn.Discover(); err != nil {
				t.Fatal(err)
			}
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
